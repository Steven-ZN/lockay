#include "apply.h"
#include "filebuf.h"
#include "lockdb.h"
#include "validate.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ---------- Internal helpers ---------- */

/* Hash context lines around a region */
static void hash_context(const FileBuf *fb, int start, int end,
                         Sha256Digest *before, Sha256Digest *after) {
    int ctx = 3;
    /* Before context */
    int bs = start - ctx, be = start - 1;
    if (bs < 1) bs = 1;
    if (be < 1) {
        memset(before, 0, sizeof(*before));
    } else {
        const char **ptrs = malloc((size_t)(be - bs + 1) * sizeof(char *));
        for (int i = bs; i <= be; i++)
            ptrs[i - bs] = fb->lines[i - 1].text;
        sha256_hash_lines(ptrs, be - bs + 1, before);
        free(ptrs);
    }

    /* After context */
    int as = end + 1, ae = end + ctx;
    if (as > fb->count) as = fb->count + 1;
    if (ae > fb->count) ae = fb->count;
    if (as > ae) {
        memset(after, 0, sizeof(*after));
    } else {
        const char **ptrs = malloc((size_t)(ae - as + 1) * sizeof(char *));
        for (int i = as; i <= ae; i++)
            ptrs[i - as] = fb->lines[i - 1].text;
        sha256_hash_lines(ptrs, ae - as + 1, after);
        free(ptrs);
    }
}

static char *full_path(const char *repo_root, const char *file) {
    size_t len = strlen(repo_root) + strlen(file) + 4;
    char *p = malloc(len);
    snprintf(p, len, "%s/%s", repo_root, file);
    return p;
}

/* ---------- Show ---------- */

int apply_show(const char *repo_root, const char *file,
               int start, int end) {
    char *fpath = full_path(repo_root, file);
    FileBuf *fb = filebuf_load(fpath);
    free(fpath);
    if (!fb) {
        fprintf(stderr, "ERROR: cannot open %s\n", file);
        return APPLY_ERROR;
    }

    LockDB *db = lockdb_load(repo_root);

    if (start < 1) start = 1;
    if (end < 0 || end > fb->count) end = fb->count;
    if (start > end) {
        fprintf(stderr, "ERROR: invalid range %d-%d (start > end)\n", start, end);
        lockdb_free(db);
        filebuf_free(fb);
        return APPLY_ERROR;
    }

    for (int i = start; i <= end; i++) {
        LockRecord *lr = lockdb_is_line_locked(db, file, i);
        const char *marker = lr ? " █" : "  ";
        const char *info   = "";

        /* Show lock info on first line of each lock region */
        if (lr && i == lr->start) {
            /* Need a static buffer for the info string */
            static char buf[128];
            snprintf(buf, sizeof(buf), "  [locked: %s]", lr->id);
            info = buf;
        }

        printf("%s%4d | %s%s\n", marker, i,
               fb->lines[i - 1].text, info);
    }

    /* Summary of editable/locked regions */
    int editable_lines = 0;
    for (int i = 1; i <= fb->count; i++) {
        if (!lockdb_is_line_locked(db, file, i))
            editable_lines++;
    }

    if (db->count > 0) {
        LockRecord *fl[64];
        int n = lockdb_get_file_locks(db, file, fl, 64);
        if (n > 0) {
            printf("\n--- Locked regions (%d) ---\n", n);
            for (int i = 0; i < n; i++)
                printf("  %s: lines %d-%d  owner:%s  %s\n",
                       fl[i]->id, fl[i]->start, fl[i]->end,
                       fl[i]->owner, fl[i]->reason);
        }
        printf("Editable lines: %d / %d\n", editable_lines, fb->count);
    }

    lockdb_free(db);
    filebuf_free(fb);
    return APPLY_OK;
}

/* ---------- Status ---------- */

int apply_status(const char *repo_root, const char *file) {
    LockDB *db = lockdb_load(repo_root);

    if (file) {
        LockRecord *fl[64];
        int n = lockdb_get_file_locks(db, file, fl, 64);
        printf("File: %s\n", file);
        printf("Total locks: %d\n\n", n);
        for (int i = 0; i < n; i++) {
            printf("  %s  lines %d-%d  owner: %s\n",
                   fl[i]->id, fl[i]->start, fl[i]->end, fl[i]->owner);
            if (fl[i]->reason && fl[i]->reason[0])
                printf("    reason: %s\n", fl[i]->reason);
            printf("    created: %s\n", fl[i]->created_at);
        }
    } else {
        printf("Repository: %s\n", repo_root);
        printf("Total locks: %d\n\n", db->count);
        for (int i = 0; i < db->count; i++) {
            LockRecord *lr = &db->locks[i];
            printf("  %s  %s:%d-%d  owner:%s\n",
                   lr->id, lr->file, lr->start, lr->end, lr->owner);
        }
    }

    lockdb_free(db);
    return APPLY_OK;
}

/* ---------- Lock ---------- */

int apply_lock(const char *repo_root, const char *file,
               int start, int end, const char *owner, const char *reason) {
    char *fpath = full_path(repo_root, file);
    FileBuf *fb = filebuf_load(fpath);
    free(fpath);

    if (!fb) {
        fprintf(stderr, "ERROR: cannot open %s\n", file);
        return APPLY_ERROR;
    }

    if (start < 1 || end > fb->count || start > end) {
        fprintf(stderr, "ERROR: invalid line range %d-%d (file has %d lines)\n",
                start, end, fb->count);
        filebuf_free(fb);
        return APPLY_ERROR;
    }

    LockDB *db = lockdb_load(repo_root);

    /* Check for overlapping locks */
    for (int i = start; i <= end; i++) {
        LockRecord *existing = lockdb_is_line_locked(db, file, i);
        if (existing) {
            fprintf(stderr, "ERROR: line %d is already locked by %s (lines %d-%d)\n",
                    i, existing->id, existing->start, existing->end);
            lockdb_free(db);
            filebuf_free(fb);
            return APPLY_ERROR;
        }
    }

    /* Compute hashes */
    Sha256Digest content_hash, before_hash, after_hash;
    const char **ptrs = malloc((size_t)(end - start + 1) * sizeof(char *));
    for (int i = start; i <= end; i++)
        ptrs[i - start] = fb->lines[i - 1].text;
    sha256_hash_lines(ptrs, end - start + 1, &content_hash);
    free(ptrs);

    hash_context(fb, start, end, &before_hash, &after_hash);

    const char *id = lockdb_add(db, file, start, end,
                                &content_hash, &before_hash, &after_hash,
                                owner, reason);

    if (lockdb_save(db) != 0) {
        fprintf(stderr, "ERROR: failed to save lock database\n");
        lockdb_free(db);
        filebuf_free(fb);
        return APPLY_ERROR;
    }

    char ch[65];
    sha256_hex(&content_hash, ch);
    printf("Locked %s lines %d-%d  id:%s  hash:%s\n",
           file, start, end, id, ch);

    lockdb_free(db);
    filebuf_free(fb);
    return APPLY_OK;
}

/* ---------- Unlock ---------- */

int apply_unlock(const char *repo_root, const char *id) {
    LockDB *db = lockdb_load(repo_root);
    LockRecord *lr = lockdb_find_by_id(db, id);

    if (!lr) {
        fprintf(stderr, "ERROR: lock %s not found\n", id);
        lockdb_free(db);
        return APPLY_ERROR;
    }

    printf("Unlocked %s (was %s lines %d-%d)\n", id, lr->file, lr->start, lr->end);
    lockdb_remove(db, id);

    if (lockdb_save(db) != 0) {
        fprintf(stderr, "ERROR: failed to save lock database\n");
        lockdb_free(db);
        return APPLY_ERROR;
    }

    lockdb_free(db);
    return APPLY_OK;
}

/* ---------- Check ---------- */

int apply_check(const char *repo_root, const char *file) {
    char *fpath = full_path(repo_root, file);
    FileBuf *fb = filebuf_load(fpath);
    free(fpath);

    if (!fb) {
        fprintf(stderr, "ERROR: cannot open %s\n", file);
        return APPLY_ERROR;
    }

    LockDB *db = lockdb_load(repo_root);
    ValidationResult vr = validate_locks(db, file, fb);

    if (vr.passed) {
        printf("OK: all locks intact in %s\n", file);
    } else {
        printf("VIOLATION: %s\n", vr.error_msg);
    }

    validation_result_free(&vr);
    lockdb_free(db);
    filebuf_free(fb);
    return vr.passed ? APPLY_OK : APPLY_DENIED;
}

/* ---------- Patch ---------- */

/* Apply a simple unified diff patch. Returns number of hunks applied
 * or -1 on error. This is a minimal implementation that handles
 * the most common diff format produced by diff -u / git diff. */
static int apply_diff(FileBuf *fb, const char *patch_text) {
    const char *p = patch_text;
    int applied = 0;

    while (*p) {
        /* Skip to next @@ hunk header */
        const char *hunk = strstr(p, "\n@@");
        if (!hunk) hunk = strstr(p, "@@");
        if (!hunk) break;

        /* If we found \n@@, skip the \n */
        if (*hunk == '\n') hunk++;
        p = hunk;

        /* Parse @@ -old_start,old_count +new_start,new_count @@ */
        int old_start, old_count, new_start, new_count;
        int n = sscanf(hunk, "@@ -%d,%d +%d,%d @@",
                       &old_start, &old_count, &new_start, &new_count);
        if (n < 3) {
            /* Try without counts: @@ -old_start +new_start @@ */
            n = sscanf(hunk, "@@ -%d +%d @@", &old_start, &new_start);
            if (n < 2) { p++; continue; }
            old_count = 1;
            new_count = 1;
        } else if (n == 3) {
            /* @@ -old_start,old_count +new_start @@ */
            new_count = 1;
        }

        /* Move past the @@ line */
        p = strchr(p, '\n');
        if (!p) break;
        p++; /* Now at first line of the hunk */

        /* Collect context, - and + lines */
        int num_del = 0, num_add = 0;
        char *add_lines[256];
        int del_start_line = old_start;

        while (*p && *p != '@' && !(*p == '-' && *(p+1) == '-' && *(p+2) == '-')) {
            if (*p == '\n') { p++; continue; }
            if (strncmp(p, "\\ ", 2) == 0) {
                /* \ No newline at end of file — skip */
                const char *nl = strchr(p, '\n');
                p = nl ? nl + 1 : p + strlen(p);
                continue;
            }

            if (*p == '-') {
                num_del++;
            } else if (*p == '+') {
                /* Extract added line content */
                const char *start = p + 1;
                const char *end = strchr(start, '\n');
                size_t linelen = end ? (size_t)(end - start) : strlen(start);
                char *line = malloc(linelen + 1);
                memcpy(line, start, linelen);
                line[linelen] = '\0';
                if (num_add < 256) add_lines[num_add++] = line;
                else free(line);
            } else if (*p == ' ') {
                /* Context line — advances position in original */
                del_start_line++;
            }

            const char *nl = strchr(p, '\n');
            if (nl) p = nl + 1;
            else break;
        }

        /* Apply the hunk: delete lines then insert new ones */
        /* old_start is relative to original file */
        if (num_del > 0)
            filebuf_delete_lines(fb, old_start, old_start + num_del - 1);

        /* Insert added lines */
        int insert_at = (num_del > 0) ? old_start : del_start_line;
        for (int i = num_add - 1; i >= 0; i--) {
            filebuf_insert_line(fb, insert_at, add_lines[i]);
            free(add_lines[i]);
        }

        applied++;
    }

    return applied;
}

int apply_patch(const char *repo_root, const char *file,
                const char *patch_path) {
    /* Read patch file */
    FILE *pf = fopen(patch_path, "r");
    if (!pf) {
        fprintf(stderr, "ERROR: cannot open patch file %s: %s\n",
                patch_path, strerror(errno));
        return APPLY_ERROR;
    }
    fseek(pf, 0, SEEK_END);
    long psize = ftell(pf);
    rewind(pf);
    char *patch_text = malloc((size_t)psize + 1);
    if (!patch_text) { fclose(pf); return APPLY_ERROR; }
    size_t nread = fread(patch_text, 1, (size_t)psize, pf);
    patch_text[nread] = '\0';
    fclose(pf);

    /* Load original file */
    char *fpath = full_path(repo_root, file);
    FileBuf *fb = filebuf_load(fpath);
    if (!fb) {
        fprintf(stderr, "ERROR: cannot open %s\n", file);
        free(patch_text);
        free(fpath);
        return APPLY_ERROR;
    }

    /* Validate: find which lines the patch would affect */
    LockDB *db = lockdb_load(repo_root);

    /* Apply patch tentatively */
    int hunks = apply_diff(fb, patch_text);
    free(patch_text);

    if (hunks < 0) {
        fprintf(stderr, "ERROR: failed to apply patch\n");
        lockdb_free(db);
        filebuf_free(fb);
        free(fpath);
        return APPLY_ERROR;
    }

    /* Validate locks after patch */
    ValidationResult vr = validate_locks(db, file, fb);
    if (!vr.passed) {
        fprintf(stderr, "DENIED: %s\n", vr.error_msg);
        validation_result_free(&vr);
        lockdb_free(db);
        filebuf_free(fb);
        free(fpath);
        return APPLY_DENIED;
    }

    /* Re-anchor and save lockdb */
    validate_reanchor(db, file, fb);
    if (lockdb_save(db) != 0) {
        fprintf(stderr, "ERROR: failed to save lock database\n");
        lockdb_free(db);
        filebuf_free(fb);
        free(fpath);
        return APPLY_ERROR;
    }

    /* Atomic save */
    if (filebuf_save_atomic(fb, fpath) != 0) {
        fprintf(stderr, "ERROR: failed to save file\n");
        lockdb_free(db);
        filebuf_free(fb);
        free(fpath);
        return APPLY_ERROR;
    }

    printf("OK: patch applied (%d hunks)\n", hunks);

    lockdb_free(db);
    filebuf_free(fb);
    free(fpath);
    return APPLY_OK;
}

/* ---------- Line-level apply (for agent) ---------- */

static int apply_line_op(const char *repo_root, const char *file,
                         int line, int end_line,
                         const char *text, int op) {
    /* op: 0=set, 1=insert, 2=delete */
    char *fpath = full_path(repo_root, file);
    FileBuf *fb = filebuf_load(fpath);
    if (!fb) {
        fprintf(stderr, "ERROR: cannot open %s\n", file);
        free(fpath);
        return APPLY_ERROR;
    }

    LockDB *db = lockdb_load(repo_root);

    /* Check if operation touches locked lines */
    int check_start, check_end;
    switch (op) {
    case 0: /* set */
        check_start = check_end = line;
        break;
    case 1: /* insert */
        /* Insert before line doesn't affect locked lines if the insert
         * point isn't inside a locked region. But after insertion, locked
         * regions shift. We just need to check that the insertion point
         * isn't about to break a lock by splitting locked content. */
        check_start = line;
        check_end = (line > fb->count) ? fb->count : line;
        break;
    case 2: /* delete */
        check_start = line;
        check_end = end_line;
        break;
    default:
        check_start = check_end = 1;
    }

    /* For set and delete, every affected line must be unlocked */
    if (op == 0 || op == 2) {
        for (int i = check_start; i <= check_end; i++) {
            LockRecord *lr = lockdb_is_line_locked(db, file, i);
            if (lr) {
                fprintf(stderr, "DENIED: line %d is locked by %s (lines %d-%d)\n",
                        i, lr->id, lr->start, lr->end);
                lockdb_free(db);
                filebuf_free(fb);
                free(fpath);
                return APPLY_DENIED;
            }
        }
    }

    /* Perform operation */
    int rc = 0;
    switch (op) {
    case 0: rc = filebuf_set_line(fb, line, text);    break;
    case 1: rc = filebuf_insert_line(fb, line, text); break;
    case 2: rc = filebuf_delete_lines(fb, line, end_line); break;
    }

    if (rc != 0) {
        fprintf(stderr, "ERROR: operation failed\n");
        lockdb_free(db);
        filebuf_free(fb);
        free(fpath);
        return APPLY_ERROR;
    }

    /* Validate all locks after the edit */
    ValidationResult vr = validate_locks(db, file, fb);
    if (!vr.passed) {
        fprintf(stderr, "DENIED: %s\n", vr.error_msg);
        validation_result_free(&vr);
        lockdb_free(db);
        filebuf_free(fb);
        free(fpath);
        return APPLY_DENIED;
    }

    /* Re-anchor and save */
    validate_reanchor(db, file, fb);
    lockdb_save(db);

    /* Atomic save */
    if (filebuf_save_atomic(fb, fpath) != 0) {
        fprintf(stderr, "ERROR: failed to save file\n");
        lockdb_free(db);
        filebuf_free(fb);
        free(fpath);
        return APPLY_ERROR;
    }

    printf("OK\n");
    lockdb_free(db);
    filebuf_free(fb);
    free(fpath);
    return APPLY_OK;
}

int apply_set_line(const char *repo_root, const char *file,
                   int line, const char *text) {
    return apply_line_op(repo_root, file, line, line, text, 0);
}

int apply_insert_line(const char *repo_root, const char *file,
                      int before_line, const char *text) {
    return apply_line_op(repo_root, file, before_line, before_line, text, 1);
}

int apply_delete_lines(const char *repo_root, const char *file,
                       int start, int end) {
    return apply_line_op(repo_root, file, start, end, NULL, 2);
}
