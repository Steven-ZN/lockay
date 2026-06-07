#include "lockdb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ---------- Lock database path ---------- */

static char *lockdb_path(const char *repo_root) {
    size_t len = strlen(repo_root) + 32;
    char *p = malloc(len);
    snprintf(p, len, "%s/.linelock", repo_root);
    return p;
}

static char *locks_file_path(const char *repo_root) {
    size_t len = strlen(repo_root) + 32;
    char *p = malloc(len);
    snprintf(p, len, "%s/.linelock/locks", repo_root);
    return p;
}

/* ---------- Load / Save ---------- */

LockDB *lockdb_load(const char *repo_root) {
    LockDB *db = calloc(1, sizeof(*db));
    db->repo_root = strdup(repo_root);
    db->db_path   = locks_file_path(repo_root);
    db->cap        = 64;
    db->locks      = calloc((size_t)db->cap, sizeof(LockRecord));

    FILE *f = fopen(db->db_path, "r");
    if (!f) return db;  /* No locks yet — empty db */

    char *line = NULL;
    size_t linesz = 0;
    while (getline(&line, &linesz, f) != -1) {
        /* Skip empty lines and comments */
        if (line[0] == '\n' || line[0] == '#' || line[0] == '\0')
            continue;

        /* Strip newline */
        size_t llen = strlen(line);
        if (llen > 0 && line[llen - 1] == '\n') line[--llen] = '\0';
        if (llen > 0 && line[llen - 1] == '\r') line[--llen] = '\0';

        /* Parse: file|id|start|end|content_sha256|before_sha256|after_sha256|commit|owner|created|reason */

        char *file       = line;
        char *id         = strchr(file, '|');      if (!id)   continue; *id++ = '\0';
        char *start_s    = strchr(id, '|');         if (!start_s) continue; *start_s++ = '\0';
        char *end_s      = strchr(start_s, '|');    if (!end_s)   continue; *end_s++ = '\0';
        char *content_h  = strchr(end_s, '|');      if (!content_h) continue; *content_h++ = '\0';
        char *before_h   = strchr(content_h, '|');  if (!before_h) continue; *before_h++ = '\0';
        char *after_h    = strchr(before_h, '|');   if (!after_h) continue; *after_h++ = '\0';
        char *commit     = strchr(after_h, '|');    if (!commit)  continue; *commit++ = '\0';
        char *owner      = strchr(commit, '|');      if (!owner)  continue; *owner++ = '\0';
        char *created    = strchr(owner, '|');      if (!created) continue; *created++ = '\0';
        char *reason     = strchr(created, '|');    if (!reason) continue; *reason++ = '\0';

        if (db->count >= db->cap) {
            db->cap *= 2;
            db->locks = realloc(db->locks, (size_t)db->cap * sizeof(LockRecord));
        }

        LockRecord *lr = &db->locks[db->count];
        memset(lr, 0, sizeof(*lr));
        lr->file   = strdup(file);
        strncpy(lr->id, id, sizeof(lr->id) - 1);
        lr->start  = atoi(start_s);
        lr->end    = atoi(end_s);
        lr->line_count = lr->end - lr->start + 1;
        sha256_parse_hex(content_h, &lr->content_hash);
        sha256_parse_hex(before_h, &lr->before_hash);
        sha256_parse_hex(after_h,  &lr->after_hash);
        strncpy(lr->commit, commit, sizeof(lr->commit) - 1);
        lr->commit[sizeof(lr->commit) - 1] = '\0';
        lr->owner     = strdup(owner);
        lr->created_at = strdup(created);
        lr->reason    = strdup(reason);

        db->count++;
    }

    free(line);
    fclose(f);
    return db;
}

int lockdb_save(const LockDB *db) {
    /* Ensure .linelock directory exists */
    char *dotdir = lockdb_path(db->repo_root);
    mkdir(dotdir, 0755);
    free(dotdir);

    FILE *f = fopen(db->db_path, "w");
    if (!f) return -1;

    fprintf(f, "# linelock database v1\n");
    fprintf(f, "# file|id|start|end|content_sha256|before_sha256|after_sha256|commit|owner|created|reason\n");
    fprintf(f, "# Generated: %s\n\n", "auto");

    for (int i = 0; i < db->count; i++) {
        LockRecord *lr = &db->locks[i];
        char ch[65], bh[65], ah[65];
        sha256_hex(&lr->content_hash, ch);
        sha256_hex(&lr->before_hash,  bh);
        sha256_hex(&lr->after_hash,   ah);

        fprintf(f, "%s|%s|%d|%d|%s|%s|%s|%s|%s|%s|%s\n",
                lr->file, lr->id, lr->start, lr->end,
                ch, bh, ah, lr->commit,
                lr->owner, lr->created_at, lr->reason);
    }

    fclose(f);
    return 0;
}

void lockdb_free(LockDB *db) {
    if (!db) return;
    for (int i = 0; i < db->count; i++) {
        LockRecord *lr = &db->locks[i];
        free(lr->file);
        free(lr->owner);
        free(lr->reason);
        free(lr->created_at);
    }
    free(db->locks);
    free(db->db_path);
    free(db->repo_root);
    free(db);
}

/* ---------- ID generation ---------- */

void lockdb_gen_id(char out[8]) {
    unsigned char rnd[3];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, rnd, 3) != 3) {
            /* Fallback on short read */
            unsigned int mix = (unsigned)(time(NULL) ^ getpid());
            for (int i = 0; i < 3; i++) rnd[i] = (unsigned char)(mix >> (i * 8));
        }
        close(fd);
    } else {
        unsigned int mix = (unsigned)(time(NULL) ^ getpid());
        for (int i = 0; i < 3; i++) rnd[i] = (unsigned char)(mix >> (i * 8));
    }
    for (int i = 0; i < 6; i++)
        out[i] = "0123456789abcdef"[(rnd[i/2] >> ((i%2) ? 0 : 4)) & 0xf];
    out[6] = '\0';
}

static bool lockdb_id_exists(const LockDB *db, const char *id) {
    for (int i = 0; i < db->count; i++)
        if (strcmp(db->locks[i].id, id) == 0)
            return true;
    return false;
}

/* ---------- CRUD ---------- */

const char *lockdb_add(LockDB *db, const char *file,
                       int start, int end,
                       const Sha256Digest *content_hash,
                       const Sha256Digest *before_hash,
                       const Sha256Digest *after_hash,
                       const char *commit,
                       const char *owner, const char *reason) {
    if (db->count >= db->cap) {
        db->cap *= 2;
        db->locks = realloc(db->locks, (size_t)db->cap * sizeof(LockRecord));
    }

    LockRecord *lr = &db->locks[db->count];
    memset(lr, 0, sizeof(*lr));
    lr->file       = strdup(file);

    /* Generate unique ID with collision retry (max 10 attempts) */
    for (int retry = 0; retry < 10; retry++) {
        lockdb_gen_id(lr->id);
        if (!lockdb_id_exists(db, lr->id)) break;
    }
    lr->start      = start;
    lr->end        = end;
    lr->line_count = end - start + 1;
    lr->content_hash = *content_hash;
    lr->before_hash  = *before_hash;
    lr->after_hash   = *after_hash;
    strncpy(lr->commit, commit ? commit : "", sizeof(lr->commit) - 1);
    lr->commit[sizeof(lr->commit) - 1] = '\0';
    lr->owner      = strdup(owner ? owner : "unknown");
    lr->reason     = strdup(reason ? reason : "");

    /* Timestamp */
    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    lr->created_at = strdup(ts);

    db->count++;
    return lr->id;
}

int lockdb_remove(LockDB *db, const char *id) {
    for (int i = 0; i < db->count; i++) {
        if (strcmp(db->locks[i].id, id) == 0) {
            /* Free record */
            free(db->locks[i].file);
            free(db->locks[i].owner);
            free(db->locks[i].reason);
            free(db->locks[i].created_at);

            /* Shift remaining */
            if (i < db->count - 1) {
                memmove(&db->locks[i], &db->locks[i + 1],
                        (size_t)(db->count - i - 1) * sizeof(LockRecord));
            }
            db->count--;
            return 0;
        }
    }
    return -1;
}

LockRecord *lockdb_find_by_id(LockDB *db, const char *id) {
    for (int i = 0; i < db->count; i++)
        if (strcmp(db->locks[i].id, id) == 0)
            return &db->locks[i];
    return NULL;
}

int lockdb_get_file_locks(LockDB *db, const char *file,
                          LockRecord **out, int max) {
    int cnt = 0;
    for (int i = 0; i < db->count && cnt < max; i++)
        if (strcmp(db->locks[i].file, file) == 0)
            out[cnt++] = &db->locks[i];
    return cnt;
}

LockRecord *lockdb_is_line_locked(LockDB *db, const char *file, int line) {
    for (int i = 0; i < db->count; i++) {
        LockRecord *lr = &db->locks[i];
        if (strcmp(lr->file, file) == 0 &&
            line >= lr->start && line <= lr->end)
            return lr;
    }
    return NULL;
}

void lockdb_update_range(LockDB *db, const char *id,
                         int new_start, int new_end) {
    LockRecord *lr = lockdb_find_by_id(db, id);
    if (!lr) return;
    lr->start = new_start;
    lr->end   = new_end;
    lr->line_count = new_end - new_start + 1;
}
