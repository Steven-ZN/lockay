#include "validate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha256.h"

/* Context lines for re-anchoring */
#define CONTEXT_LINES  3
#define SEARCH_WINDOW  50

/* Compute hash of lines [start, end] in the buffer */
static void hash_buffer_range(const FileBuf *fb, int start, int end,
                               Sha256Digest *out) {
    /* Adjust to valid range */
    if (start < 1) start = 1;
    if (end > fb->count) end = fb->count;

    int count = end - start + 1;
    if (count <= 0) {
        memset(out, 0, sizeof(*out));
        return;
    }

    const char **ptrs = malloc((size_t)count * sizeof(char *));
    for (int i = 0; i < count; i++)
        ptrs[i] = fb->lines[start - 1 + i].text;

    sha256_hash_lines(ptrs, count, out);
    free(ptrs);
}

/* Re-anchor a single lock: find its content in the buffer.
 * Updates lr->start and lr->end if found at a new position.
 * Returns true if found (content intact), false if not found (modified). */
static bool reanchor_one(const FileBuf *fb, LockRecord *lr) {
    int lc = lr->line_count;

    /* Try current position first */
    Sha256Digest cur_hash;
    hash_buffer_range(fb, lr->start, lr->end, &cur_hash);
    if (sha256_eq(&cur_hash, &lr->content_hash))
        return true;

    /* Search nearby: within SEARCH_WINDOW of original position */
    int search_start = lr->start - SEARCH_WINDOW;
    if (search_start < 1) search_start = 1;
    int search_end = lr->start + SEARCH_WINDOW;
    if (search_end + lc - 1 > fb->count) search_end = fb->count - lc + 1;

    for (int s = search_start; s <= search_end; s++) {
        Sha256Digest h;
        hash_buffer_range(fb, s, s + lc - 1, &h);
        if (sha256_eq(&h, &lr->content_hash)) {
            /* Found — update position */
            lr->start = s;
            lr->end   = s + lc - 1;
            lr->line_count = lc;
            return true;
        }
    }

    /* Full file scan as last resort */
    for (int s = 1; s <= fb->count - lc + 1; s++) {
        Sha256Digest h;
        hash_buffer_range(fb, s, s + lc - 1, &h);
        if (sha256_eq(&h, &lr->content_hash)) {
            lr->start = s;
            lr->end   = s + lc - 1;
            lr->line_count = lc;
            return true;
        }
    }

    return false;  /* Content was modified — lock violation */
}

/* ---------- Public API ---------- */

ValidationResult validate_locks(LockDB *db, const char *file,
                                const FileBuf *fb) {
    ValidationResult vr = { .passed = true, .error_msg = NULL,
                            .violated_lock = NULL };

    /* Collect locks for this file */
    LockRecord *file_locks[256];
    int n = lockdb_get_file_locks(db, file, file_locks, 256);

    for (int i = 0; i < n; i++) {
        LockRecord *lr = file_locks[i];
        if (!reanchor_one(fb, lr)) {
            /* Lock violation */
            size_t msglen = strlen(lr->file) + strlen(lr->id) + 200;
            vr.error_msg = malloc(msglen);
            snprintf(vr.error_msg, msglen,
                     "Lock violation: region %s in %s (was lines %d-%d) "
                     "has been modified. Locked content must not change.\n"
                     "  Lock ID: %s\n"
                     "  Owner: %s\n"
                     "  Reason: %s",
                     lr->id, lr->file,
                     lr->start, lr->end,
                     lr->id, lr->owner, lr->reason);
            vr.passed = false;
            vr.violated_lock = lr;
            return vr;
        }
    }

    return vr;
}

int validate_reanchor(LockDB *db, const char *file, const FileBuf *fb) {
    LockRecord *file_locks[256];
    int n = lockdb_get_file_locks(db, file, file_locks, 256);
    int reanchored = 0;

    for (int i = 0; i < n; i++) {
        LockRecord *lr = file_locks[i];
        int old_start = lr->start;

        if (!reanchor_one(fb, lr))
            return -1;

        if (lr->start != old_start) {
            /* Update in the database too */
            lockdb_update_range(db, lr->id, lr->start, lr->end);
            reanchored++;
        }
    }

    return reanchored;
}

void validation_result_free(ValidationResult *vr) {
    free(vr->error_msg);
    vr->error_msg = NULL;
}
