#ifndef APPLY_H
#define APPLY_H

/* Exit codes for apply operations */
#define APPLY_OK       0
#define APPLY_DENIED   1
#define APPLY_ERROR    2

/* Apply a unified diff patch. Returns APPLY_OK/APPLY_DENIED/APPLY_ERROR. */
int apply_patch(const char *repo_root, const char *file,
                const char *patch_path);

/* Line-level operations (agent-facing CLI) */
int apply_set_line(const char *repo_root, const char *file,
                   int line, const char *text);
int apply_insert_line(const char *repo_root, const char *file,
                      int before_line, const char *text);
int apply_delete_lines(const char *repo_root, const char *file,
                       int start, int end);

/* Show file contents with lock annotations */
int apply_show(const char *repo_root, const char *file,
               int start, int end);

/* Show lock status for a file */
int apply_status(const char *repo_root, const char *file);

/* Lock lines in a file. Returns lock id on success (printed to stdout). */
int apply_lock(const char *repo_root, const char *file,
               int start, int end, const char *owner, const char *reason);

/* Unlock by lock id */
int apply_unlock(const char *repo_root, const char *id);

/* Check lock integrity for a file */
int apply_check(const char *repo_root, const char *file);

#endif
