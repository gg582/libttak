#include <ttak/security/sha256.h>
#include "test_macros.h"
#include <string.h>
#include <stdio.h>

void test_sha256_basic() {
    SHA256_CTX ctx;
    uint8_t hash[SHA256_BLOCK_SIZE];
    const char *input = "hello world";
    /* Basic regression vector from FIPS 180-4. */
    const uint8_t expected_hash[] = {
        0xb9, 0x4d, 0x27, 0xb9, 0x93, 0x4d, 0x3e, 0x08, 0xa5, 0x2e, 0x52, 0xd7, 0xda, 0x7d, 0xab, 0xfa,
        0xc4, 0x84, 0xef, 0xe3, 0x7a, 0x53, 0x80, 0xee, 0x90, 0x88, 0xf7, 0xac, 0xe2, 0xef, 0xcd, 0xe9
    };

    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)input, strlen(input));
    sha256_final(&ctx, hash);

    ASSERT(memcmp(hash, expected_hash, SHA256_BLOCK_SIZE) == 0);

    // Test with empty string
    const char *empty_input = "";
    /* Empty-string vector catches padding mistakes. */
    const uint8_t expected_empty_hash[] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)empty_input, strlen(empty_input));
    sha256_final(&ctx, hash);
    ASSERT(memcmp(hash, expected_empty_hash, SHA256_BLOCK_SIZE) == 0);
}

int main() {
    RUN_TEST(test_sha256_basic);
    return 0;
}
