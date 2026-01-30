#ifndef MERSENNE_APP_TYPES_H
#define MERSENNE_APP_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdalign.h>

/**
 * @brief GIMPS Result structure for reporting and retry queue.
 * 
 * Memory Layout:
 * +-------------------+-------------------+-------------------+
 * | p (uint32_t)      | residue (uint64_t)| is_prime (bool)   |
 * +-------------------+-------------------+-------------------+
 * | status (int)      | padding (64b)                         |
 * +-------------------+-------------------+-------------------+
 */
typedef struct {
    uint32_t p;
    uint64_t residue;
    bool     is_prime;
    int      status; // 0: pending, 1: reported
    alignas(64) char padding[0];
} gimps_result_t;

/**
 * @brief Application State for persistence and GIMPS metadata.
 */
typedef struct {
    char     computerid[64];
    char     userid[64];
    uint32_t last_p;
    size_t   result_count;
    gimps_result_t *results;
} app_state_t;

#endif // MERSENNE_APP_TYPES_H