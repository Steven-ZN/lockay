#include "filebuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define FILEBUF_INIT_CAP 256
#define LINE_INIT_CAP    128

/* ---------- Lifecycle ---------- */

FileBuf *filebuf_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    FileBuf *fb = calloc(1, sizeof(*fb));
    if (!fb) { fclose(f); return NULL; }

    fb->path = strdup(path);
    fb->cap  = FILEBUF_INIT_CAP;
    fb->lines = calloc((size_t)fb->cap, sizeof(Line));

    char *raw = NULL;
    size_t rawlen = 0;
    while (getline(&raw, &rawlen, f) != -1) {
        size_t len = strlen(raw);

        /* Strip trailing \n and \r */
        if (len > 0 && raw[len - 1] == '\n') raw[--len] = '\0';
        if (len > 0 && raw[len - 1] == '\r') raw[--len] = '\0';

        if (fb->count >= fb->cap) {
            fb->cap *= 2;
            fb->lines = realloc(fb->lines, (size_t)fb->cap * sizeof(Line));
        }

        Line *ln = &fb->lines[fb->count];
        ln->len = len;
        ln->cap = len + 16;
        ln->text = malloc(ln->cap);
        memcpy(ln->text, raw, len + 1);

        fb->count++;
    }
    free(raw);
    fclose(f);
    return fb;
}

void filebuf_free(FileBuf *fb) {
    if (!fb) return;
    for (int i = 0; i < fb->count; i++)
        free(fb->lines[i].text);
    free(fb->lines);
    free(fb->path);
    free(fb);
}

int filebuf_save(const FileBuf *fb, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    for (int i = 0; i < fb->count; i++) {
        if (fwrite(fb->lines[i].text, 1, fb->lines[i].len, f) != fb->lines[i].len)
            { fclose(f); return -1; }
        if (fputc('\n', f) == EOF)
            { fclose(f); return -1; }
    }
    fclose(f);
    return 0;
}

int filebuf_save_atomic(const FileBuf *fb, const char *path) {
    /* Create temp file in same directory for atomic rename */
    const char *last_slash = strrchr(path, '/');
    const char *dir, *fname;

    if (last_slash) {
        size_t dirlen = (size_t)(last_slash - path);
        char *d = malloc(dirlen + 1);
        memcpy(d, path, dirlen);
        d[dirlen] = '\0';
        dir = d;
        fname = last_slash + 1;
    } else {
        dir = ".";
        fname = path;
    }

    size_t tmplen = strlen(dir) + strlen(fname) + 20;
    char *tmppath = malloc(tmplen);
    snprintf(tmppath, tmplen, "%s/.%s.linelock.XXXXXX", dir, fname);

    int fd = mkstemp(tmppath);
    if (fd < 0) {
        if (last_slash) free((void *)dir);
        free(tmppath);
        return -1;
    }

    /* Set permissions to match typical repo files */
    fchmod(fd, 0644);

    /* Write all lines */
    FILE *tmpf = fdopen(fd, "w");
    if (!tmpf) {
        close(fd); unlink(tmppath);
        if (last_slash) free((void *)dir);
        free(tmppath);
        return -1;
    }

    for (int i = 0; i < fb->count; i++) {
        if (fwrite(fb->lines[i].text, 1, fb->lines[i].len, tmpf) != fb->lines[i].len
            || fputc('\n', tmpf) == EOF) {
            fclose(tmpf); unlink(tmppath);
            if (last_slash) free((void *)dir);
            free(tmppath);
            return -1;
        }
    }

    if (fflush(tmpf) != 0 || fsync(fileno(tmpf)) != 0) {
        fclose(tmpf); unlink(tmppath);
        if (last_slash) free((void *)dir);
        free(tmppath);
        return -1;
    }
    fclose(tmpf);

    if (rename(tmppath, path) != 0) {
        unlink(tmppath);
        if (last_slash) free((void *)dir);
        free(tmppath);
        return -1;
    }

    if (last_slash) free((void *)dir);
    free(tmppath);
    return 0;
}

/* ---------- Line-level operations ---------- */

static int line_ensure_cap(Line *ln, size_t need) {
    if (need + 1 <= ln->cap) return 0;
    size_t newcap = ln->cap ? ln->cap * 2 : LINE_INIT_CAP;
    while (newcap <= need) newcap *= 2;
    char *newtext = realloc(ln->text, newcap);
    if (!newtext) return -1;
    ln->text = newtext;
    ln->cap = newcap;
    return 0;
}

static int ensure_line_capacity(FileBuf *fb) {
    if (fb->count >= fb->cap) {
        int newcap = fb->cap ? fb->cap * 2 : FILEBUF_INIT_CAP;
        Line *newlines = realloc(fb->lines, (size_t)newcap * sizeof(Line));
        if (!newlines) return -1;
        fb->lines = newlines;
        /* Zero-initialize new slots */
        memset(fb->lines + fb->cap, 0, (size_t)(newcap - fb->cap) * sizeof(Line));
        fb->cap = newcap;
    }
    return 0;
}

int filebuf_set_line(FileBuf *fb, int line, const char *text) {
    if (line < 1 || line > fb->count) return -1;
    Line *ln = &fb->lines[line - 1];
    size_t tlen = strlen(text);
    if (line_ensure_cap(ln, tlen) != 0) return -1;
    memcpy(ln->text, text, tlen + 1);
    ln->len = tlen;
    fb->modified = true;
    return 0;
}

int filebuf_insert_line(FileBuf *fb, int before_line, const char *text) {
    if (before_line < 1 || before_line > fb->count + 1) return -1;
    if (ensure_line_capacity(fb) != 0) return -1;

    int idx = before_line - 1;

    /* Shift lines down */
    memmove(&fb->lines[idx + 1], &fb->lines[idx],
            (size_t)(fb->count - idx) * sizeof(Line));

    /* Init new line */
    size_t tlen = strlen(text);
    fb->lines[idx].text = malloc(tlen + 16);
    fb->lines[idx].cap  = tlen + 16;
    fb->lines[idx].len  = tlen;
    memcpy(fb->lines[idx].text, text, tlen + 1);

    fb->count++;
    fb->modified = true;
    return 0;
}

int filebuf_delete_lines(FileBuf *fb, int start, int end) {
    if (start < 1 || end > fb->count || start > end) return -1;
    int num = end - start + 1;

    /* Free deleted lines */
    for (int i = start - 1; i < end; i++)
        free(fb->lines[i].text);

    /* Shift remaining lines up */
    memmove(&fb->lines[start - 1], &fb->lines[end],
            (size_t)(fb->count - end) * sizeof(Line));
    fb->count -= num;
    fb->modified = true;
    return 0;
}

/* ---------- Character-level editing ---------- */

int filebuf_char_insert(FileBuf *fb, int line, int col, int ch) {
    if (line < 1 || line > fb->count) return -1;
    Line *ln = &fb->lines[line - 1];
    if (col < 0 || col > (int)ln->len) return -1;

    if (line_ensure_cap(ln, ln->len + 1) != 0) return -1;

    memmove(ln->text + col + 1, ln->text + col, ln->len - (size_t)col + 1);
    ln->text[col] = (char)ch;
    ln->len++;
    fb->modified = true;
    return 0;
}

int filebuf_char_delete(FileBuf *fb, int line, int col) {
    if (line < 1 || line > fb->count) return -1;
    Line *ln = &fb->lines[line - 1];
    if (col < 0 || col >= (int)ln->len) return -1;

    memmove(ln->text + col, ln->text + col + 1, ln->len - (size_t)col);
    ln->len--;
    fb->modified = true;
    return 0;
}

int filebuf_line_break(FileBuf *fb, int line, int col) {
    /* Split line at col: first part stays, second part becomes new line */
    if (line < 1 || line > fb->count) return -1;
    Line *ln = &fb->lines[line - 1];
    if (col < 0 || col > (int)ln->len) return -1;

    /* Text after cursor */
    char *rest = strdup(ln->text + col);
    if (!rest) return -1;

    /* Truncate current line */
    ln->text[col] = '\0';
    ln->len = (size_t)col;

    /* Insert rest as new line */
    int rc = filebuf_insert_line(fb, line + 1, rest);
    free(rest);
    return rc;
}

int filebuf_line_join(FileBuf *fb, int line) {
    /* Join this line with the next one */
    if (line < 1 || line >= fb->count) return -1;

    Line *cur  = &fb->lines[line - 1];
    Line *next = &fb->lines[line];
    size_t newlen = cur->len + next->len;

    if (line_ensure_cap(cur, newlen) != 0) return -1;

    memcpy(cur->text + cur->len, next->text, next->len + 1);
    cur->len = newlen;

    /* Remove next line */
    filebuf_delete_lines(fb, line + 1, line + 1);
    /* delete_lines already sets modified, no need to set again */
    return 0;
}

/* ---------- Access ---------- */

const char *filebuf_get_line(const FileBuf *fb, int line) {
    if (line < 1 || line > fb->count) return NULL;
    return fb->lines[line - 1].text;
}

void filebuf_dump(const FileBuf *fb, int start, int end) {
    if (start < 1) start = 1;
    if (end > fb->count) end = fb->count;
    for (int i = start; i <= end; i++)
        printf("%s\n", fb->lines[i - 1].text);
}
