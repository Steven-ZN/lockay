#ifndef FILEBUF_H
#define FILEBUF_H

#include <stddef.h>
#include <stdbool.h>

/* A single line with dynamic capacity for in-place editing */
typedef struct {
    char  *text;   /* null-terminated, no trailing newline */
    size_t len;    /* strlen(text) */
    size_t cap;    /* allocated size of text buffer */
} Line;

/* Line-based file buffer with character-level editing */
typedef struct {
    Line *lines;
    int   count;       /* number of lines */
    int   cap;         /* allocated capacity */
    char *path;        /* original file path (owned) */
    bool  modified;
} FileBuf;

/* Lifecycle */
FileBuf *filebuf_load(const char *path);
void     filebuf_free(FileBuf *fb);
int      filebuf_save(const FileBuf *fb, const char *path);
int      filebuf_save_atomic(const FileBuf *fb, const char *path);

/* Line-level operations — return 0 on success, -1 on error */
int filebuf_set_line(FileBuf *fb, int line, const char *text);
int filebuf_insert_line(FileBuf *fb, int before_line, const char *text);
int filebuf_delete_lines(FileBuf *fb, int start, int end);

/* Character-level editing — return 0 on success, -1 on error */
int filebuf_char_insert(FileBuf *fb, int line, int col, int ch);
int filebuf_char_delete(FileBuf *fb, int line, int col);
int filebuf_line_break(FileBuf *fb, int line, int col);
int filebuf_line_join(FileBuf *fb, int line);

/* Get line (1-indexed), returns NULL for out-of-bounds */
const char *filebuf_get_line(const FileBuf *fb, int line);

/* Debug: dump buffer to stdout */
void filebuf_dump(const FileBuf *fb, int start, int end);

#endif
