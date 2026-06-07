#ifndef VALIDATE_H
#define VALIDATE_H

#include <stdbool.h>
#include "filebuf.h"
#include "lockdb.h"

/* Result of lock validation */
typedef struct {
    bool         passed;
    char        *error_msg;      /* set on failure, must be freed */
    LockRecord  *violated_lock;  /* which lock was violated */
} ValidationResult;

/* Validate that all locks for a file are intact in the buffer.
 * Attempts re-anchoring if line numbers have shifted.
 * Returns a result that must be freed with validation_result_free(). */
ValidationResult validate_locks(LockDB *db, const char *file,
                                const FileBuf *fb);

/* Free a validation result */
void validation_result_free(ValidationResult *vr);

/* Re-anchor all locks for a file: update line ranges based on content
 * matching. Returns number of re-anchored locks, or -1 on failure
 * (meaning locked content was modified). */
int validate_reanchor(LockDB *db, const char *file, const FileBuf *fb);

#endif
