#include "sha256.h"
#include "filebuf.h"
#include "lockdb.h"
#include "validate.h"
#include "apply.h"
#include "cmdlock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

static int tests_run  = 0;
static int tests_pass = 0;
static int tests_fail = 0;

#define TEST(name) \
    static void test_##name(void); \
    __attribute__((constructor)) static void reg_##name(void) {} \
    static void test_##name(void)

#define RUN(name) do { \
    tests_run++; \
    printf("  %-50s ", #name); \
    fflush(stdout); \
    test_##name(); \
    tests_pass++; \
    printf("PASSED\n"); \
} while(0)

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("FAILED: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        tests_fail++; \
        return; \
    } \
} while(0)

/* ================================================================
 * Test helpers
 * ================================================================ */

static char *tmpdir(void) {
    char *d = strdup("/tmp/linelock-test.XXXXXX");
    char *r = mkdtemp(d);
    if (!r) { free(d); return NULL; }
    return d;
}

static void rmrf(const char *path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    if (system(cmd) != 0) { /* cleanup best-effort */ }
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(content, f);
    fclose(f);
}

/* ================================================================
 * SHA-256 Tests
 * ================================================================ */

TEST(sha256_basic) {
    Sha256Digest d;
    sha256_hash_str("hello world", &d);

    char hex[65];
    sha256_hex(&d, hex);
    CHECK(strlen(hex) == 64, "hex length");

    /* Known hash of "hello world" */
    CHECK(strcmp(hex, "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9") == 0,
          "sha256('hello world')");
}

TEST(sha256_empty) {
    Sha256Digest d;
    sha256_hash((const uint8_t *)"", 0, &d);

    char hex[65];
    sha256_hex(&d, hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0,
          "sha256('')");
}

TEST(sha256_lines) {
    const char *lines[] = {"line1", "line2", "line3"};
    Sha256Digest d;
    sha256_hash_lines(lines, 3, &d);

    /* Should match hashing "line1\nline2\nline3\n" */
    Sha256Digest d2;
    sha256_hash_str("line1\nline2\nline3\n", &d2);
    CHECK(sha256_eq(&d, &d2), "sha256_lines matches joined hash");
}

TEST(sha256_eq) {
    Sha256Digest a, b;
    sha256_hash_str("foo", &a);
    sha256_hash_str("foo", &b);
    CHECK(sha256_eq(&a, &b), "same strings produce same hash");

    Sha256Digest c;
    sha256_hash_str("bar", &c);
    CHECK(!sha256_eq(&a, &c), "different strings produce different hash");
}

TEST(sha256_parse_hex) {
    Sha256Digest a, b;
    sha256_hash_str("test", &a);
    char hex[65];
    sha256_hex(&a, hex);

    CHECK(sha256_parse_hex(hex, &b), "parse hex");
    CHECK(sha256_eq(&a, &b), "round-trip");
}

/* ================================================================
 * FileBuf Tests
 * ================================================================ */

TEST(filebuf_load_save) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", dir);

    write_file(path, "line one\nline two\nline three\n");

    FileBuf *fb = filebuf_load(path);
    CHECK(fb != NULL, "load file");
    CHECK(fb->count == 3, "line count");
    CHECK(strcmp(fb->lines[0].text, "line one") == 0, "line 1 content");
    CHECK(strcmp(fb->lines[1].text, "line two") == 0, "line 2 content");
    CHECK(strcmp(fb->lines[2].text, "line three") == 0, "line 3 content");

    /* Save to new path */
    char save_path[256];
    snprintf(save_path, sizeof(save_path), "%s/out.txt", dir);
    CHECK(filebuf_save(fb, save_path) == 0, "save file");

    /* Verify saved content */
    FileBuf *fb2 = filebuf_load(save_path);
    CHECK(fb2->count == 3, "reloaded line count");
    CHECK(strcmp(fb2->lines[0].text, "line one") == 0, "reloaded line 1");

    filebuf_free(fb);
    filebuf_free(fb2);
    rmrf(dir);
}

TEST(filebuf_empty_file) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/empty.txt", dir);
    write_file(path, "");

    FileBuf *fb = filebuf_load(path);
    CHECK(fb != NULL, "load empty file");
    CHECK(fb->count == 0, "empty file has 0 lines");

    filebuf_free(fb);
    rmrf(dir);
}

TEST(filebuf_set_line) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", dir);
    write_file(path, "hello\nworld\n");

    FileBuf *fb = filebuf_load(path);
    filebuf_set_line(fb, 1, "HELLO");
    CHECK(strcmp(fb->lines[0].text, "HELLO") == 0, "set line 1");
    CHECK(fb->modified, "modified flag set");

    filebuf_free(fb);
    rmrf(dir);
}

TEST(filebuf_insert_line) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", dir);
    write_file(path, "first\nthird\n");

    FileBuf *fb = filebuf_load(path);
    CHECK(fb->count == 2, "initial count");

    /* Insert between */
    CHECK(filebuf_insert_line(fb, 2, "second") == 0, "insert at 2");
    CHECK(fb->count == 3, "count after insert");
    CHECK(strcmp(fb->lines[0].text, "first") == 0, "line 1 unchanged");
    CHECK(strcmp(fb->lines[1].text, "second") == 0, "line 2 inserted");
    CHECK(strcmp(fb->lines[2].text, "third") == 0, "line 3 shifted");

    /* Insert at beginning */
    filebuf_insert_line(fb, 1, "zeroth");
    CHECK(strcmp(fb->lines[0].text, "zeroth") == 0, "insert at beginning");

    /* Insert at end */
    filebuf_insert_line(fb, fb->count + 1, "fourth");
    CHECK(strcmp(fb->lines[fb->count - 1].text, "fourth") == 0, "insert at end");

    filebuf_free(fb);
    rmrf(dir);
}

TEST(filebuf_delete_lines) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", dir);
    write_file(path, "one\ntwo\nthree\nfour\nfive\n");

    FileBuf *fb = filebuf_load(path);
    CHECK(fb->count == 5, "initial count");

    /* Delete middle */
    filebuf_delete_lines(fb, 2, 4);
    CHECK(fb->count == 2, "count after delete");
    CHECK(strcmp(fb->lines[0].text, "one") == 0, "line 1 kept");
    CHECK(strcmp(fb->lines[1].text, "five") == 0, "line 2 is old line 5");

    filebuf_free(fb);
    rmrf(dir);
}

TEST(filebuf_char_insert) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", dir);
    write_file(path, "hello\n");

    FileBuf *fb = filebuf_load(path);
    filebuf_char_insert(fb, 1, 5, '!');
    CHECK(strcmp(fb->lines[0].text, "hello!") == 0, "char insert at end");

    filebuf_char_insert(fb, 1, 0, '>');
    CHECK(strcmp(fb->lines[0].text, ">hello!") == 0, "char insert at beginning");

    filebuf_char_insert(fb, 1, 3, 'X');
    CHECK(strcmp(fb->lines[0].text, ">heXllo!") == 0, "char insert in middle");

    filebuf_free(fb);
    rmrf(dir);
}

TEST(filebuf_char_delete) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", dir);
    write_file(path, "hello\n");

    FileBuf *fb = filebuf_load(path);
    filebuf_char_delete(fb, 1, 1);  /* delete 'e' */
    CHECK(strcmp(fb->lines[0].text, "hllo") == 0, "char delete");

    filebuf_free(fb);
    rmrf(dir);
}

TEST(filebuf_line_break) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", dir);
    write_file(path, "hello world\n");

    FileBuf *fb = filebuf_load(path);
    filebuf_line_break(fb, 1, 6);  /* split at space */
    CHECK(fb->count == 2, "two lines after break");
    CHECK(strcmp(fb->lines[0].text, "hello ") == 0, "first part");
    CHECK(strcmp(fb->lines[1].text, "world") == 0, "second part");

    filebuf_free(fb);
    rmrf(dir);
}

TEST(filebuf_line_join) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", dir);
    write_file(path, "hello\nworld\n");

    FileBuf *fb = filebuf_load(path);
    filebuf_line_join(fb, 1);
    CHECK(fb->count == 1, "one line after join");
    CHECK(strcmp(fb->lines[0].text, "helloworld") == 0, "joined content");

    filebuf_free(fb);
    rmrf(dir);
}

TEST(filebuf_atomic_save) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", dir);
    write_file(path, "original content\n");

    FileBuf *fb = filebuf_load(path);
    filebuf_set_line(fb, 1, "modified content");

    CHECK(filebuf_save_atomic(fb, path) == 0, "atomic save");

    /* Verify content */
    FileBuf *fb2 = filebuf_load(path);
    CHECK(strcmp(fb2->lines[0].text, "modified content") == 0, "saved correctly");

    filebuf_free(fb);
    filebuf_free(fb2);
    rmrf(dir);
}

TEST(filebuf_crlf_handling) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.txt", dir);
    write_file(path, "line1\r\nline2\r\n");

    FileBuf *fb = filebuf_load(path);
    CHECK(fb->count == 2, "CRLF line count");
    CHECK(strcmp(fb->lines[0].text, "line1") == 0, "CRLF stripped");
    CHECK(strcmp(fb->lines[1].text, "line2") == 0, "CRLF stripped");

    filebuf_free(fb);
    rmrf(dir);
}

/* ================================================================
 * LockDB Tests
 * ================================================================ */

TEST(lockdb_add_find) {
    char *dir = tmpdir();
    mkdir(dir, 0755);
    char dotdir[256];
    snprintf(dotdir, sizeof(dotdir), "%s/.linelock", dir);
    mkdir(dotdir, 0755);

    LockDB *db = lockdb_load(dir);
    CHECK(db != NULL, "load empty db");
    CHECK(db->count == 0, "empty db has 0 locks");

    Sha256Digest ch, bh, ah;
    sha256_hash_str("content", &ch);
    sha256_hash_str("before",  &bh);
    sha256_hash_str("after",   &ah);

    const char *id = lockdb_add(db, "test.c", 10, 20, &ch, &bh, &ah,
                                 "", "steven", "test lock");
    CHECK(id != NULL, "add lock returns id");
    CHECK(db->count == 1, "db has 1 lock");

    LockRecord *lr = lockdb_find_by_id(db, id);
    CHECK(lr != NULL, "find by id");
    CHECK(strcmp(lr->file, "test.c") == 0, "file matches");
    CHECK(lr->start == 10 && lr->end == 20, "range matches");
    CHECK(sha256_eq(&lr->content_hash, &ch), "content hash matches");
    CHECK(strcmp(lr->owner, "steven") == 0, "owner matches");
    CHECK(strcmp(lr->reason, "test lock") == 0, "reason matches");

    lockdb_free(db);
    rmrf(dir);
}

TEST(lockdb_save_load) {
    char *dir = tmpdir();

    /* Create locks */
    {
        LockDB *db = lockdb_load(dir);
        Sha256Digest ch, bh, ah;
        sha256_hash_str("alpha", &ch);
        sha256_hash_str("bef",   &bh);
        sha256_hash_str("aft",   &ah);
        lockdb_add(db, "a.c", 1, 3, &ch, &bh, &ah, "", "user1", "first");
        lockdb_add(db, "b.c", 5, 8, &ch, &bh, &ah, "", "user2", "second");
        lockdb_save(db);
        lockdb_free(db);
    }

    /* Reload and verify */
    {
        LockDB *db = lockdb_load(dir);
        CHECK(db->count == 2, "reloaded 2 locks");
        CHECK(strcmp(db->locks[1].file, "b.c") == 0, "second lock file");
        CHECK(strcmp(db->locks[1].owner, "user2") == 0, "second lock owner");
        lockdb_free(db);
    }

    rmrf(dir);
}

TEST(lockdb_remove) {
    char *dir = tmpdir();
    LockDB *db = lockdb_load(dir);

    Sha256Digest ch, bh, ah;
    sha256_hash_str("x", &ch);
    sha256_hash_str("y", &bh);
    sha256_hash_str("z", &ah);

    const char *id1 = lockdb_add(db, "f.c", 1, 1, &ch, &bh, &ah, "", "o", "r");
    const char *id2 = lockdb_add(db, "g.c", 2, 2, &ch, &bh, &ah, "", "o", "r");
    CHECK(db->count == 2, "two locks");

    /* Save id strings before remove (pointers into db become invalid) */
    char save_id1[16], save_id2[16];
    snprintf(save_id1, sizeof(save_id1), "%s", id1);
    snprintf(save_id2, sizeof(save_id2), "%s", id2);

    CHECK(lockdb_remove(db, save_id1) == 0, "remove first");
    CHECK(db->count == 1, "one left");
    CHECK(lockdb_find_by_id(db, save_id1) == NULL, "first gone");
    CHECK(lockdb_find_by_id(db, save_id2) != NULL, "second still there");

    CHECK(lockdb_remove(db, "nonexistent") == -1, "remove nonexistent returns -1");

    lockdb_free(db);
    rmrf(dir);
}

TEST(lockdb_is_line_locked) {
    char *dir = tmpdir();
    LockDB *db = lockdb_load(dir);

    Sha256Digest ch;
    sha256_hash_str("x", &ch);

    lockdb_add(db, "f.c", 10, 20, &ch, &ch, &ch, "", "o", "r");

    CHECK(lockdb_is_line_locked(db, "f.c", 5) == NULL, "line 5 not locked");
    CHECK(lockdb_is_line_locked(db, "f.c", 10) != NULL, "line 10 locked");
    CHECK(lockdb_is_line_locked(db, "f.c", 15) != NULL, "line 15 locked");
    CHECK(lockdb_is_line_locked(db, "f.c", 20) != NULL, "line 20 locked");
    CHECK(lockdb_is_line_locked(db, "f.c", 21) == NULL, "line 21 not locked");
    CHECK(lockdb_is_line_locked(db, "g.c", 15) == NULL, "different file");

    lockdb_free(db);
    rmrf(dir);
}

TEST(lockdb_file_locks) {
    char *dir = tmpdir();
    LockDB *db = lockdb_load(dir);

    Sha256Digest ch;
    sha256_hash_str("x", &ch);

    lockdb_add(db, "a.c", 1, 1, &ch, &ch, &ch, "", "o", "");
    lockdb_add(db, "a.c", 5, 5, &ch, &ch, &ch, "", "o", "");
    lockdb_add(db, "b.c", 3, 3, &ch, &ch, &ch, "", "o", "");

    LockRecord *fl[8];
    int n = lockdb_get_file_locks(db, "a.c", fl, 8);
    CHECK(n == 2, "file a.c has 2 locks");

    n = lockdb_get_file_locks(db, "b.c", fl, 8);
    CHECK(n == 1, "file b.c has 1 lock");

    n = lockdb_get_file_locks(db, "c.c", fl, 8);
    CHECK(n == 0, "file c.c has 0 locks");

    lockdb_free(db);
    rmrf(dir);
}

/* ================================================================
 * Validate Tests
 * ================================================================ */

TEST(validate_no_locks) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.c", dir);
    write_file(path, "int main() {\n    return 0;\n}\n");

    FileBuf *fb = filebuf_load(path);
    LockDB  *db = lockdb_load(dir);

    ValidationResult vr = validate_locks(db, "test.c", fb);
    CHECK(vr.passed, "no locks = validation passes");

    validation_result_free(&vr);
    filebuf_free(fb);
    lockdb_free(db);
    rmrf(dir);
}

TEST(validate_intact_lock) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.c", dir);
    write_file(path, "line 1\nline 2 locked\nline 3 locked\nline 4\n");

    FileBuf *fb = filebuf_load(path);
    LockDB  *db = lockdb_load(dir);

    /* Lock lines 2-3 */
    Sha256Digest ch, bh, ah;
    const char *l2[] = { fb->lines[1].text };
    const char *l3[] = { fb->lines[2].text };
    const char *locked_lines[] = { fb->lines[1].text, fb->lines[2].text };
    sha256_hash_lines(locked_lines, 2, &ch);
    sha256_hash_lines(l2, 1, &bh);  /* before context doesn't matter much here */
    sha256_hash_lines(l3, 1, &ah);

    lockdb_add(db, "test.c", 2, 3, &ch, &bh, &ah, "", "steven", "test");

    ValidationResult vr = validate_locks(db, "test.c", fb);
    CHECK(vr.passed, "unchanged lock passes");

    validation_result_free(&vr);
    filebuf_free(fb);
    lockdb_free(db);
    rmrf(dir);
}

TEST(validate_violated_lock) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.c", dir);
    write_file(path, "line 1\nline 2 locked\nline 3 locked\nline 4\n");

    FileBuf *fb = filebuf_load(path);
    LockDB  *db = lockdb_load(dir);

    /* Lock lines 2-3 */
    Sha256Digest ch, bh, ah;
    const char *locked[] = { fb->lines[1].text, fb->lines[2].text };
    sha256_hash_lines(locked, 2, &ch);
    sha256_hash_str("before", &bh);
    sha256_hash_str("after", &ah);

    lockdb_add(db, "test.c", 2, 3, &ch, &bh, &ah, "", "steven", "test");

    /* Modify locked content */
    filebuf_set_line(fb, 2, "modified content!");

    ValidationResult vr = validate_locks(db, "test.c", fb);
    CHECK(!vr.passed, "modified lock fails validation");
    CHECK(vr.error_msg != NULL, "error message set");
    CHECK(vr.violated_lock != NULL, "violated lock set");

    validation_result_free(&vr);
    filebuf_free(fb);
    lockdb_free(db);
    rmrf(dir);
}

TEST(validate_reanchor) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.c", dir);
    write_file(path, "header\nlocked A\nlocked B\nfooter\n");

    FileBuf *fb = filebuf_load(path);
    LockDB  *db = lockdb_load(dir);

    /* Lock lines 2-3 */
    const char *locked[] = { fb->lines[1].text, fb->lines[2].text };
    Sha256Digest ch, bh, ah;
    sha256_hash_lines(locked, 2, &ch);
    sha256_hash_str("b", &bh);
    sha256_hash_str("a", &ah);

    lockdb_add(db, "test.c", 2, 3, &ch, &bh, &ah, "", "steven", "test");

    /* Insert lines before lock — locked content shifts down */
    filebuf_insert_line(fb, 1, "new header line 1");
    filebuf_insert_line(fb, 2, "new header line 2");

    /* Locked content should now be at lines 4-5 */
    int reanchored = validate_reanchor(db, "test.c", fb);
    CHECK(reanchored >= 1, "locks re-anchored after insert");

    LockRecord *lr = lockdb_is_line_locked(db, "test.c", 4);
    CHECK(lr != NULL, "lock now at line 4 (was 2)");
    CHECK(lr->start == 4 && lr->end == 5, "range updated to 4-5");

    /* Validation should pass */
    ValidationResult vr = validate_locks(db, "test.c", fb);
    CHECK(vr.passed, "re-anchored lock passes");

    validation_result_free(&vr);
    filebuf_free(fb);
    lockdb_free(db);
    rmrf(dir);
}

/* ================================================================
 * Apply Tests (integration)
 * ================================================================ */

TEST(apply_set_line_allowed) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.c", dir);
    write_file(path, "line 1\nline 2\nline 3\n");

    int rc = apply_set_line(dir, "test.c", 2, "LINE TWO");
    CHECK(rc == APPLY_OK, "set unlocked line");

    /* Verify */
    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[1].text, "LINE TWO") == 0, "line changed");
    filebuf_free(fb);
    rmrf(dir);
}

TEST(apply_set_line_denied) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.c", dir);
    write_file(path, "line 1\nline 2 locked\nline 3\n");

    /* Lock line 2 first */
    int rc = apply_lock(dir, "test.c", 2, 2, "steven", "important");
    CHECK(rc == APPLY_OK, "lock created");

    /* Try to set locked line */
    rc = apply_set_line(dir, "test.c", 2, "MODIFIED");
    CHECK(rc == APPLY_DENIED, "set locked line denied");

    /* Content should be unchanged */
    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[1].text, "line 2 locked") == 0, "locked line unchanged");
    filebuf_free(fb);
    rmrf(dir);
}

TEST(apply_insert_before_lock) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.c", dir);
    write_file(path, "header\nlocked content\nfooter\n");

    /* Lock line 2 */
    char *lock_hash;
    {
        FileBuf *fb = filebuf_load(path);
        const char *lines[] = { fb->lines[1].text };
        Sha256Digest ch;
        sha256_hash_lines(lines, 1, &ch);
        char hex[65];
        sha256_hex(&ch, hex);
        lock_hash = strdup(hex);
        filebuf_free(fb);
    }
    apply_lock(dir, "test.c", 2, 2, "steven", "protected");

    /* Insert before lock */
    int rc = apply_insert_line(dir, "test.c", 2, "new line before lock");
    CHECK(rc == APPLY_OK, "insert before lock allowed");

    /* Verify lock content still intact */
    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[2].text, "locked content") == 0, "locked content untouched");
    CHECK(strcmp(fb->lines[1].text, "new line before lock") == 0, "new line inserted");

    /* Lock should be re-anchored */
    LockDB *db = lockdb_load(dir);
    LockRecord *lr = lockdb_is_line_locked(db, "test.c", 2);
    CHECK(lr == NULL, "line 2 is NOT the locked line (it's the new insert)");
    CHECK(lockdb_is_line_locked(db, "test.c", 3) != NULL, "line 3 IS the locked line now");

    lockdb_free(db);
    filebuf_free(fb);
    free(lock_hash);
    rmrf(dir);
}

TEST(apply_delete_locked_line) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.c", dir);
    write_file(path, "line 1\nlocked\nline 3\n");

    apply_lock(dir, "test.c", 2, 2, "steven", "no delete");

    /* Try to delete locked line */
    int rc = apply_delete_lines(dir, "test.c", 2, 2);
    CHECK(rc == APPLY_DENIED, "delete locked line denied");

    /* Line should still be there */
    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[1].text, "locked") == 0, "locked line still exists");
    filebuf_free(fb);
    rmrf(dir);
}

TEST(apply_patch_allowed) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.py", dir);
    write_file(path, "def foo():\n    x = 1\n    y = 2\n    return x + y\n");

    int rc = apply_lock(dir, "test.py", 1, 1, "steven", "public API");
    CHECK(rc == APPLY_OK, "locked line 1");

    /* Create patch that only modifies editable lines */
    char patch_path[256];
    snprintf(patch_path, sizeof(patch_path), "%s/patch.diff", dir);
    write_file(patch_path,
               "--- a/test.py\n"
               "+++ b/test.py\n"
               "@@ -2,3 +2,3 @@\n"
               " def foo():\n"
               "-    x = 1\n"
               "+    x = 100\n"
               "     y = 2\n");

    rc = apply_patch(dir, "test.py", patch_path);
    CHECK(rc == APPLY_OK, "patch applied to unlocked region");

    /* Verify */
    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[0].text, "def foo():") == 0, "locked line unchanged");
    CHECK(strcmp(fb->lines[1].text, "    x = 100") == 0, "unlocked line changed");
    filebuf_free(fb);
    rmrf(dir);
}

TEST(apply_patch_touching_locked) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.py", dir);
    write_file(path, "def public_api():\n    return 42\n\ndef helper():\n    return 0\n");

    /* Lock public_api */
    apply_lock(dir, "test.py", 1, 2, "steven", "public contract");

    /* Create patch that tries to modify the locked region */
    char patch_path[256];
    snprintf(patch_path, sizeof(patch_path), "%s/patch.diff", dir);
    write_file(patch_path,
               "--- a/test.py\n"
               "+++ b/test.py\n"
               "@@ -1,2 +1,2 @@\n"
               "-def public_api():\n"
               "-    return 42\n"
               "+def public_api():\n"
               "+    return 999\n");  /* changed return value */

    int rc = apply_patch(dir, "test.py", patch_path);
    CHECK(rc == APPLY_DENIED, "patch modifying locked region denied");

    /* Verify original content intact */
    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[1].text, "    return 42") == 0, "locked content unchanged");
    filebuf_free(fb);
    rmrf(dir);
}

TEST(apply_patch_insert_before_lock) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.py", dir);
    write_file(path, "def public():\n    return 0\n\ndef helper():\n    pass\n");

    /* Lock public function */
    apply_lock(dir, "test.py", 1, 2, "steven", "api");

    /* Insert new code BEFORE the locked region */
    char patch_path[256];
    snprintf(patch_path, sizeof(patch_path), "%s/patch.diff", dir);
    write_file(patch_path,
               "--- a/test.py\n"
               "+++ b/test.py\n"
               "@@ -1,0 +1,3 @@\n"
               "+import sys\n"
               "+import os\n"
               "+\n");

    int rc = apply_patch(dir, "test.py", patch_path);
    CHECK(rc == APPLY_OK, "insert before locked region allowed");

    /* Verify lock re-anchored */
    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[3].text, "def public():") == 0, "locked function shifted down");
    filebuf_free(fb);
    rmrf(dir);
}

TEST(apply_status) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.c", dir);
    write_file(path, "a\nb\nc\n");

    apply_lock(dir, "test.c", 2, 2, "steven", "test");
    apply_lock(dir, "test.c", 3, 3, "steven", "test2");

    int rc = apply_status(dir, "test.c");
    CHECK(rc == APPLY_OK, "status works");
    rmrf(dir);
}

TEST(apply_check_ok) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.c", dir);
    write_file(path, "line1\nline2 locked\nline3\n");

    apply_lock(dir, "test.c", 2, 2, "steven", "check test");

    int rc = apply_check(dir, "test.c");
    CHECK(rc == APPLY_OK, "check passes");

    rmrf(dir);
}

TEST(apply_check_violation) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/test.c", dir);
    write_file(path, "line1\nline2 locked\nline3\n");

    apply_lock(dir, "test.c", 2, 2, "steven", "check test");

    /* Modify file outside of linelock (simulating bypass) */
    write_file(path, "line1\nMODIFIED!!\nline3\n");

    int rc = apply_check(dir, "test.c");
    CHECK(rc == APPLY_DENIED, "check detects violation");

    rmrf(dir);
}

/* ================================================================
 * Integration: full workflow
 * ================================================================ */

TEST(integration_workflow) {
    char *dir = tmpdir();

    /* 1. Create a file */
    char path[256];
    snprintf(path, sizeof(path), "%s/model.py", dir);

    /* 2. Edit via linelock style */
    {
        write_file(path, "class Model:\n    def __init__(self):\n        self.layer = 1\n\n    def forward(self, x):\n        return x + 1\n\n    def helper(self):\n        pass\n");
    }

    /* 3. Lock the forward method (lines 4-5) */
    int rc = apply_lock(dir, "model.py", 4, 5, "steven", "public API contract");
    CHECK(rc == APPLY_OK, "lock forward method");

    /* 4. Lock the __init__ method (lines 2-3) */
    rc = apply_lock(dir, "model.py", 2, 3, "steven", "init contract");
    CHECK(rc == APPLY_OK, "lock init method");

    /* 5. Show status */
    rc = apply_status(dir, "model.py");
    CHECK(rc == APPLY_OK, "show status");

    /* 6. Try to modify locked line (should fail) */
    rc = apply_set_line(dir, "model.py", 4, "    def forward(self, x, y):");
    CHECK(rc == APPLY_DENIED, "modify locked line denied");

    /* 7. Modify editable line (line 7: helper) */
    rc = apply_set_line(dir, "model.py", 7, "    def helper(self, data):");
    CHECK(rc == APPLY_OK, "modify unlocked line allowed");

    /* 8. Insert new editable method */
    rc = apply_insert_line(dir, "model.py", 8, "    def new_feature(self):");
    CHECK(rc == APPLY_OK, "insert new code");

    /* 9. Verify locks still intact. Lines 4-5 (forward) and 2-3 (init) are locked.
     * Original file had 9 lines; we modified line 7 and inserted at 8 = now 10 lines.
     * Forward was at lines 4-5, still at 4-5 since edits were below it. */
    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[4].text, "    def forward(self, x):") == 0, "forward line");
    CHECK(strcmp(fb->lines[5].text, "        return x + 1") == 0, "return line");
    CHECK(strcmp(fb->lines[0].text, "class Model:") == 0, "class line unchanged");
    CHECK(strcmp(fb->lines[1].text, "    def __init__(self):") == 0, "init unchanged");
    filebuf_free(fb);

    /* 10. Check lock integrity */
    rc = apply_check(dir, "model.py");
    CHECK(rc == APPLY_OK, "final check passes");

    rmrf(dir);
}

TEST(integration_multiple_locks_reanchor) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/file", dir);
    write_file(path, "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n");

    /* Lock three separate regions */
    apply_lock(dir, "file", 2, 2, "steven", "lock b");
    apply_lock(dir, "file", 5, 6, "steven", "lock e-f");
    apply_lock(dir, "file", 9, 9, "steven", "lock i");

    /* Insert at beginning — all locks should shift down */
    apply_insert_line(dir, "file", 1, "HEADER1");
    apply_insert_line(dir, "file", 1, "HEADER2");

    /* Verify all locks re-anchored */
    int rc = apply_check(dir, "file");
    CHECK(rc == APPLY_OK, "all locks re-anchored after bulk insert");

    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[3].text, "b") == 0, "lock b at line 3 (was 2)");
    CHECK(strcmp(fb->lines[6].text, "e") == 0, "lock e at line 6 (was 5)");
    CHECK(strcmp(fb->lines[7].text, "f") == 0, "lock f at line 7 (was 6)");
    filebuf_free(fb);

    rmrf(dir);
}

/* ================================================================
 * Edge cases
 * ================================================================ */

TEST(edge_lock_single_line) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/single.txt", dir);
    write_file(path, "only line\n");

    int rc = apply_lock(dir, "single.txt", 1, 1, "steven", "only");
    CHECK(rc == APPLY_OK, "lock single line file");

    rc = apply_set_line(dir, "single.txt", 1, "changed");
    CHECK(rc == APPLY_DENIED, "cannot change locked line");

    /* No other lines to edit */
    rc = apply_check(dir, "single.txt");
    CHECK(rc == APPLY_OK, "check passes");

    rmrf(dir);
}

TEST(edge_lock_entire_file) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/all.c", dir);
    write_file(path, "line1\nline2\nline3\n");

    int rc = apply_lock(dir, "all.c", 1, 3, "steven", "entire file");
    CHECK(rc == APPLY_OK, "lock entire file");

    /* Every edit should fail */
    rc = apply_set_line(dir, "all.c", 1, "new");
    CHECK(rc == APPLY_DENIED, "set line 1 denied");

    rc = apply_set_line(dir, "all.c", 2, "new");
    CHECK(rc == APPLY_DENIED, "set line 2 denied");

    rc = apply_set_line(dir, "all.c", 3, "new");
    CHECK(rc == APPLY_DENIED, "set line 3 denied");

    /* Insert should also be denied? No, insert doesn't modify locked content */
    rc = apply_insert_line(dir, "all.c", 1, "new line");
    CHECK(rc == APPLY_OK, "insert before locked content is allowed");

    rmrf(dir);
}

TEST(edge_unlock_nonexistent) {
    char *dir = tmpdir();
    int rc = apply_unlock(dir, "nonexistent");
    CHECK(rc == APPLY_ERROR, "unlock nonexistent fails");
    rmrf(dir);
}

/* ================================================================
 * Restore Tests
 * ================================================================ */

TEST(restore_tampered_content) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/api.py", dir);

    /* Init git so restore can use it as source of truth */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "cd %s && git init && git config user.email t@t && git config user.name t", dir);
    if (system(cmd) != 0) { /* ok */ }

    write_file(path, "def public():\n    return 42\n\n# editable\n");
    snprintf(cmd, sizeof(cmd), "cd %s && git add api.py && git commit -m init", dir);
    if (system(cmd) != 0) { /* ok */ }

    /* Lock lines 1-2 */
    int rc = apply_lock(dir, "api.py", 1, 2, "steven", "API");
    CHECK(rc == APPLY_OK, "lock created");

    /* Tamper with locked content (simulate nano) */
    write_file(path, "def hacked():\n    return 999\n\n# editable\n");

    /* Auto-restore fires on access — check detects and restores */
    rc = apply_check(dir, "api.py");
    CHECK(rc == APPLY_OK, "auto-restore fires on check");

    /* Verify restored */
    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[0].text, "def public():") == 0, "line 1 restored");
    CHECK(strcmp(fb->lines[1].text, "    return 42") == 0, "line 2 restored");
    CHECK(strcmp(fb->lines[2].text, "") == 0, "blank line intact");
    CHECK(strcmp(fb->lines[3].text, "# editable") == 0, "editable line intact");
    filebuf_free(fb);

    rmrf(dir);
}

TEST(restore_intact_noop) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/f.c", dir);
    write_file(path, "safe\n");

    apply_lock(dir, "f.c", 1, 1, "steven", "test");

    /* Restore without tampering */
    int rc = apply_restore(dir, "f.c");
    CHECK(rc == APPLY_OK, "restore on intact file is no-op");

    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[0].text, "safe") == 0, "content unchanged");
    filebuf_free(fb);

    rmrf(dir);
}

TEST(restore_nonexistent_file) {
    char *dir = tmpdir();
    int rc = apply_restore(dir, "nonexistent.txt");
    CHECK(rc == APPLY_ERROR, "restore nonexistent file returns error");
    rmrf(dir);
}

TEST(restore_git_based) {
    /* Verify git commit is stored with lock */
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/r.py", dir);

    /* Init git repo */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "cd %s && git init && git config user.email test@t && git config user.name t", dir);
    if (system(cmd) != 0) { /* ok */ }

    write_file(path, "def api():\n    return 42\n");
    snprintf(cmd, sizeof(cmd), "cd %s && git add r.py && git commit -m init", dir);
    if (system(cmd) != 0) { /* ok */ }

    apply_lock(dir, "r.py", 1, 2, "steven", "git-backed");

    /* Verify commit stored */
    LockDB *db = lockdb_load(dir);
    LockRecord *lr = lockdb_is_line_locked(db, "r.py", 1);
    CHECK(lr != NULL, "lock exists");
    CHECK(strlen(lr->commit) == 40, "git commit stored");

    /* Tamper */
    write_file(path, "def hacked():\n    return 999\n");
    int rc = apply_restore(dir, "r.py");
    CHECK(rc == APPLY_OK, "restore via git succeeds");

    /* Verify */
    FileBuf *fb = filebuf_load(path);
    CHECK(strcmp(fb->lines[0].text, "def api():") == 0, "restored from git");
    CHECK(strcmp(fb->lines[1].text, "    return 42") == 0, "restored from git");

    filebuf_free(fb);
    lockdb_free(db);
    rmrf(dir);
}

TEST(edge_lock_overlap) {
    char *dir = tmpdir();
    char path[256];
    snprintf(path, sizeof(path), "%s/overlap.c", dir);
    write_file(path, "1\n2\n3\n4\n5\n");

    apply_lock(dir, "overlap.c", 2, 4, "steven", "first lock");

    /* Try to lock overlapping region */
    int rc = apply_lock(dir, "overlap.c", 3, 5, "steven", "overlapping");
    CHECK(rc == APPLY_ERROR, "overlapping lock rejected");

    rmrf(dir);
}

/* ================================================================
 * CmdLock Tests
 * ================================================================ */

TEST(cmdlock_parse_simple) {
    CmdInfo *ci = cmd_parse("ls -la /tmp");
    CHECK(ci != NULL, "parse success");
    CHECK(ci->argc == 3, "arg count");
    CHECK(strcmp(ci->argv[0], "ls") == 0, "argv[0]");
    CHECK(strcmp(ci->argv[1], "-la") == 0, "argv[1]");
    CHECK(strcmp(ci->argv[2], "/tmp") == 0, "argv[2]");
    CHECK(!ci->has_pipe, "no pipe");
    CHECK(!ci->has_redirect, "no redirect");
    cmd_info_free(ci);
}

TEST(cmdlock_parse_quoted) {
    CmdInfo *ci = cmd_parse("echo \"hello world\" 'foo bar'");
    CHECK(ci != NULL, "parse success");
    CHECK(ci->argc == 3, "arg count");
    CHECK(strcmp(ci->argv[0], "echo") == 0, "argv[0]");
    CHECK(strcmp(ci->argv[1], "hello world") == 0, "argv[1]");
    CHECK(strcmp(ci->argv[2], "foo bar") == 0, "argv[2]");
    cmd_info_free(ci);
}

TEST(cmdlock_parse_pipe) {
    CmdInfo *ci = cmd_parse("cat file.txt | grep foo");
    CHECK(ci != NULL, "parse success");
    CHECK(ci->has_pipe, "has pipe");
    cmd_info_free(ci);
}

TEST(cmdlock_parse_redirect) {
    CmdInfo *ci = cmd_parse("echo hello > out.txt");
    CHECK(ci != NULL, "parse success");
    CHECK(ci->has_redirect, "has redirect");
    CHECK(ci->redirect_path != NULL, "has redirect path");
    CHECK(strcmp(ci->redirect_path, "out.txt") == 0, "redirect path");
    cmd_info_free(ci);
}

TEST(cmdlock_risk_l0) {
    CmdInfo *ci = cmd_parse("ls -la");
    CmdRisk risk = cmd_risk_level(ci);
    CHECK(risk == RISK_L0_SAFE, "ls is L0");
    cmd_info_free(ci);

    ci = cmd_parse("cat file.txt");
    risk = cmd_risk_level(ci);
    CHECK(risk == RISK_L0_SAFE, "cat is L0");
    cmd_info_free(ci);

    ci = cmd_parse("grep pattern file");
    risk = cmd_risk_level(ci);
    CHECK(risk == RISK_L0_SAFE, "grep is L0");
    cmd_info_free(ci);
}

TEST(cmdlock_risk_l3) {
    CmdInfo *ci = cmd_parse("rm -rf build");
    CmdRisk risk = cmd_risk_level(ci);
    CHECK(risk == RISK_L3_DESTRUCT, "rm is L3");
    cmd_info_free(ci);

    ci = cmd_parse("pip install numpy");
    risk = cmd_risk_level(ci);
    CHECK(risk == RISK_L3_DESTRUCT, "pip is L3");
    cmd_info_free(ci);

    ci = cmd_parse("curl https://example.com");
    risk = cmd_risk_level(ci);
    CHECK(risk == RISK_L3_DESTRUCT, "curl is L3");
    cmd_info_free(ci);

    ci = cmd_parse("git commit -m msg");
    risk = cmd_risk_level(ci);
    CHECK(risk == RISK_L3_DESTRUCT, "git commit is L3");
    cmd_info_free(ci);
}

TEST(cmdlock_risk_l4) {
    CmdInfo *ci = cmd_parse("git push origin main");
    CmdRisk risk = cmd_risk_level(ci);
    CHECK(risk == RISK_L4_PUBLISH, "git push is L4");
    cmd_info_free(ci);

    ci = cmd_parse("sudo systemctl restart nginx");
    risk = cmd_risk_level(ci);
    CHECK(risk == RISK_L4_PUBLISH, "sudo is L4");
    cmd_info_free(ci);

    ci = cmd_parse("ssh user@host");
    risk = cmd_risk_level(ci);
    CHECK(risk == RISK_L4_PUBLISH, "ssh is L4");
    cmd_info_free(ci);

    ci = cmd_parse("chmod 777 file");
    risk = cmd_risk_level(ci);
    CHECK(risk == RISK_L4_PUBLISH, "chmod is L4");
    cmd_info_free(ci);
}

TEST(cmdlock_policy_allow) {
    char *dir = tmpdir();
    cmdlock_init(dir);

    CmdInfo *ci = cmd_parse("ls -la");
    CmdDecision d = cmdlock_check(ci);
    CHECK(d == DECISION_ALLOW, "ls is ALLOW by default policy");
    cmd_info_free(ci);

    ci = cmd_parse("pytest tests/");
    d = cmdlock_check(ci);
    CHECK(d == DECISION_ALLOW, "pytest is ALLOW by default policy");
    cmd_info_free(ci);

    cmdlock_free();
    rmrf(dir);
}

TEST(cmdlock_policy_deny) {
    char *dir = tmpdir();
    cmdlock_init(dir);

    CmdInfo *ci = cmd_parse("git push origin main");
    CmdDecision d = cmdlock_check(ci);
    CHECK(d == DECISION_DENY, "git push is DENY by default policy");
    cmd_info_free(ci);

    ci = cmd_parse("sudo rm -rf /");
    d = cmdlock_check(ci);
    CHECK(d == DECISION_DENY, "sudo is DENY by default policy");
    cmd_info_free(ci);

    cmdlock_free();
    rmrf(dir);
}

TEST(cmdlock_init_default) {
    char *dir = tmpdir();
    /* No policy file — should load defaults */
    int rc = cmdlock_init(dir);
    CHECK(rc == 0, "init with defaults succeeds");
    cmdlock_free();
    rmrf(dir);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void) {
    printf("lockay test suite\n");
    printf("====================\n\n");

    printf("SHA-256:\n");
    RUN(sha256_basic);
    RUN(sha256_empty);
    RUN(sha256_lines);
    RUN(sha256_eq);
    RUN(sha256_parse_hex);

    printf("\nFileBuf:\n");
    RUN(filebuf_load_save);
    RUN(filebuf_empty_file);
    RUN(filebuf_set_line);
    RUN(filebuf_insert_line);
    RUN(filebuf_delete_lines);
    RUN(filebuf_char_insert);
    RUN(filebuf_char_delete);
    RUN(filebuf_line_break);
    RUN(filebuf_line_join);
    RUN(filebuf_atomic_save);
    RUN(filebuf_crlf_handling);

    printf("\nLockDB:\n");
    RUN(lockdb_add_find);
    RUN(lockdb_save_load);
    RUN(lockdb_remove);
    RUN(lockdb_is_line_locked);
    RUN(lockdb_file_locks);

    printf("\nValidate:\n");
    RUN(validate_no_locks);
    RUN(validate_intact_lock);
    RUN(validate_violated_lock);
    RUN(validate_reanchor);

    printf("\nApply:\n");
    RUN(apply_set_line_allowed);
    RUN(apply_set_line_denied);
    RUN(apply_insert_before_lock);
    RUN(apply_delete_locked_line);
    RUN(apply_patch_allowed);
    RUN(apply_patch_touching_locked);
    RUN(apply_patch_insert_before_lock);
    RUN(apply_status);
    RUN(apply_check_ok);
    RUN(apply_check_violation);

    printf("\nIntegration:\n");
    RUN(integration_workflow);
    RUN(integration_multiple_locks_reanchor);

    printf("\nEdge cases:\n");
    RUN(edge_lock_single_line);
    RUN(edge_lock_entire_file);
    RUN(edge_unlock_nonexistent);
    RUN(edge_lock_overlap);

    printf("\nRestore:\n");
    RUN(restore_tampered_content);
    RUN(restore_intact_noop);
    RUN(restore_nonexistent_file);
    RUN(restore_git_based);
    printf("\nCmdLock:\n");
    RUN(cmdlock_parse_simple);
    RUN(cmdlock_parse_quoted);
    RUN(cmdlock_parse_pipe);
    RUN(cmdlock_parse_redirect);
    RUN(cmdlock_risk_l0);
    RUN(cmdlock_risk_l3);
    RUN(cmdlock_risk_l4);
    RUN(cmdlock_policy_allow);
    RUN(cmdlock_policy_deny);
    RUN(cmdlock_init_default);

    printf("\n====================\n");
    printf("Results: %d/%d passed", tests_pass, tests_run);
    if (tests_fail > 0) printf(", %d FAILED", tests_fail);
    printf("\n");

    return tests_fail > 0 ? 1 : 0;
}
