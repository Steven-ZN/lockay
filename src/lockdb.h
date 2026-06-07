#ifndef LOCKDB_H
#define LOCKDB_H

#include <stdbool.h>
#include "sha256.h"

/* A single lock record */
typedef struct {
    char    *file;           /* relative path from repo root */
    char     id[8];          /* short hex identifier */
    int      start;          /* 1-indexed start line */
    int      end;            /* 1-indexed end line (inclusive) */
    int      line_count;     /* end - start + 1 */
    Sha256Digest content_hash;  /* SHA-256 of locked lines */
    Sha256Digest before_hash;   /* SHA-256 of 3 lines before lock */
    Sha256Digest after_hash;    /* SHA-256 of 3 lines after lock */
    char    *owner;
    char    *reason;
    char    *created_at;
} LockRecord;

/* Lock database: loaded from .linelock/locks */
typedef struct {
    LockRecord *locks;
    int         count;
    int         cap;
    char       *db_path;      /* full path to .linelock/locks */
    char       *repo_root;    /* repo root directory */
} LockDB;

/* Lifecycle */
LockDB *lockdb_load(const char *repo_root);
int     lockdb_save(const LockDB *db);
void    lockdb_free(LockDB *db);

/* Add a lock. Returns lock id (owned by db, do not free). */
const char *lockdb_add(LockDB *db, const char *file,
                       int start, int end,
                       const Sha256Digest *content_hash,
                       const Sha256Digest *before_hash,
                       const Sha256Digest *after_hash,
                       const char *owner, const char *reason);

/* Remove a lock by id. Returns 0 on success, -1 if not found. */
int lockdb_remove(LockDB *db, const char *id);

/* Look up a lock by id */
LockRecord *lockdb_find_by_id(LockDB *db, const char *id);

/* Get all locks for a file (returns count, fills array up to max) */
int lockdb_get_file_locks(LockDB *db, const char *file,
                          LockRecord **out, int max);

/* Check if a line is locked. Returns lock record or NULL. */
LockRecord *lockdb_is_line_locked(LockDB *db, const char *file, int line);

/* Update a lock's line range (for re-anchoring) */
void lockdb_update_range(LockDB *db, const char *id, int new_start, int new_end);

/* Generate a short unique id */
void lockdb_gen_id(char out[8]);

#endif
