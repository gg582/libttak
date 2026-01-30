#ifndef TTAK_TIMING_H
#define TTAK_TIMING_H

#include <stdint.h>

/**
 * @brief Returns the current tick count in milliseconds.
 * 
 * Used for unified lifecycle management across the library.
 * 
 * @return Current tick count (ms).
 */
uint64_t ttak_get_tick_count(void);

#endif // TTAK_TIMING_H
