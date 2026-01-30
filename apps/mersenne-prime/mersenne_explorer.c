#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

/* --- libttak NTT & CRT Headers --- */
// Assuming these are available via your include path
#include <ttak/math/ntt.h>

/* --- Constants --- */
#define STATE_DIR "/home/yjlee/Documents"
#define STATE_FILE STATE_DIR "/found_mersenne.json"
#define STATE_FILE_TMP STATE_DIR "/found_mersenne.json.tmp"
#define MAX_ID_LEN 64
#define ALIGNMENT 64

/* --- libttak Global NTT Primes Configuration --- */
const ttak_ntt_prime_t ttak_ntt_primes[3] = {
	{ 998244353ULL, 3ULL, 23U, 17450252288407896063ULL, 299560064ULL },
	{ 1004535809ULL, 3ULL, 21U, 8214279848305098751ULL, 742115580ULL },
	{ 469762049ULL, 3ULL, 26U, 18226067692438159359ULL, 118963808ULL }
};

/* --- Data Structures --- */
typedef struct {
	uint64_t *words;
	size_t len;
	size_t cap;
} big_uint_t;

typedef struct {
	uint32_t p;
	size_t bits;
	size_t word_count;
	uint64_t last_mask;
	big_uint_t modulus;
} mersenne_mod_t;

/* --- Utility: Aligned Memory for Ryzen/N150 Cache Lines --- */
static void* ttak_aligned_alloc(size_t size) {
	void* ptr;
	if (posix_memalign(&ptr, ALIGNMENT, size) != 0) return NULL;
	memset(ptr, 0, size);
	return ptr;
}

/* --- Core BigInt Operations --- */
static void big_init(big_uint_t *n) {
	n->words = NULL;
	n->len = 0;
	n->cap = 0;
}

static void big_free(big_uint_t *n) {
	if (!n) return;
	free(n->words);
	memset(n, 0, sizeof(*n));
}

static bool big_reserve(big_uint_t *n, size_t needed) {
	if (needed <= n->cap) return true;
	size_t new_cap = ttak_next_power_of_two(needed);
	uint64_t *tmp = realloc(n->words, new_cap * sizeof(uint64_t));
	if (!tmp) return false;
	n->words = tmp;
	n->cap = new_cap;
	return true;
}

static void big_trim(big_uint_t *n) {
	while (n->len > 0 && n->words[n->len - 1] == 0) n->len--;
}

/* --- High-Performance NTT-based Squaring --- */
/* Complexity: O(n log n) instead of O(n^2) */
static bool big_square(const big_uint_t *a, big_uint_t *out) {
	size_t n = a->len;
	if (n == 0) {
		out->len = 0;
		return true;
	}

	size_t ntt_size = ttak_next_power_of_two(n * 2);
	if (!big_reserve(out, ntt_size)) return false;

	// Temporary buffers for CRT branches
	uint64_t *ntt_res[3];
	for (int i = 0; i < 3; i++) {
		ntt_res[i] = ttak_aligned_alloc(ntt_size * sizeof(uint64_t));
		memcpy(ntt_res[i], a->words, n * sizeof(uint64_t));

		// Forward NTT -> Pointwise Square -> Inverse NTT
		ttak_ntt_transform(ntt_res[i], ntt_size, &ttak_ntt_primes[i], false);
		ttak_ntt_pointwise_square(ntt_res[i], ntt_res[i], ntt_size, &ttak_ntt_primes[i]);
		ttak_ntt_transform(ntt_res[i], ntt_size, &ttak_ntt_primes[i], true);
	}

	// Combine via CRT and handle Carry Propagation
	unsigned __int128 carry = 0;
	for (size_t i = 0; i < ntt_size; i++) {
		ttak_crt_term_t terms[3];
		for (int j = 0; j < 3; j++) {
			terms[j].modulus = ttak_ntt_primes[j].modulus;
			terms[j].residue = ntt_res[j][i];
		}

		ttak_u128_t residue, modulus;
		ttak_crt_combine(terms, 3, &residue, &modulus);

		// Merge into 128-bit native for carry handling
		unsigned __int128 full_val = ((unsigned __int128)residue.hi << 64) | residue.lo;
		full_val += carry;
		out->words[i] = (uint64_t)full_val;
		carry = full_val >> 64;
	}

	out->len = ntt_size;
	big_trim(out);

	for (int i = 0; i < 3; i++) free(ntt_res[i]);
	return true;
}

/* --- Fast Mersenne Reduction: mod (2^p - 1) --- */
static bool mersenne_reduce(big_uint_t *value, const mersenne_mod_t *mod, big_uint_t *scratch) {
	while (value->len > mod->word_count ||
		(value->len == mod->word_count && (value->words[mod->word_count-1] & ~mod->last_mask))) {

		// Fast path for Mersenne primes: result = (low bits) + (high bits)
		if (!big_shift_right(value, mod->bits, scratch)) return false;

		// Mask low bits
		value->len = mod->word_count;
		value->words[mod->word_count-1] &= mod->last_mask;

	// Add back the high bits (the shift result)
	if (!big_add_assign(value, scratch)) return false;
		}
		big_trim(value);
	return true;
}

/* --- Lucas-Lehmer Implementation --- */
static int lucas_lehmer_run(uint32_t p, atomic_bool *shutdown, big_uint_t *residue) {
	if (p == 2) return 1; // M2 is prime

	mersenne_mod_t m_mod;
	mersenne_mod_init(&m_mod, p);

	big_uint_t s, square, scratch;
	big_init(&s); big_init(&square); big_init(&scratch);
	big_set_u64(&s, 4);

	for (uint32_t i = 0; i < p - 2; i++) {
		if (atomic_load(shutdown)) goto cancel;

		// S = (S^2 - 2) mod (2^p - 1)
		if (!big_square(&s, &square)) goto error;
		if (!mersenne_reduce(&square, &m_mod, &scratch)) goto error;

		// Subtract 2 with Mersenne wrap-around
		if (!big_sub_u64(&square, 2)) {
			big_add_assign(&square, &m_mod.modulus);
			big_sub_u64(&square, 2);
		}
		big_copy(&s, &square);
	}

	int is_prime = big_is_zero(&s) ? 1 : 0;

	cleanup:
	big_free(&s); big_free(&square); big_free(&scratch);
	mersenne_mod_free(&m_mod);
	return is_prime;

	cancel:
	is_prime = -1; goto cleanup;
	error:
	is_prime = -2; goto cleanup;
}

/* --- Main Logic and Persistence remains as previously defined --- */
// ... (The rest of the threading/main logic from your previous snippet)
