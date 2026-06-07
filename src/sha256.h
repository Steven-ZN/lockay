#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE    65

typedef struct {
    uint8_t bytes[SHA256_DIGEST_SIZE];
} Sha256Digest;

/* Hash raw data */
void sha256_hash(const uint8_t *data, size_t len, Sha256Digest *out);

/* Hash a range of lines (newline-separated) */
void sha256_hash_lines(const char **lines, int count, Sha256Digest *out);

/* Hash a single string */
void sha256_hash_str(const char *str, Sha256Digest *out);

/* Convert digest to hex string (65 chars including null) */
void sha256_hex(const Sha256Digest *d, char out[SHA256_HEX_SIZE]);

/* Parse hex string to digest */
bool sha256_parse_hex(const char *hex, Sha256Digest *out);

/* Compare two digests */
bool sha256_eq(const Sha256Digest *a, const Sha256Digest *b);

#endif
