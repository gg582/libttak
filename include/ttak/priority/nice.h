#ifndef TTAK_PRIORITY_NICE_H
#define TTAK_PRIORITY_NICE_H

#include <stddef.h>

/**
 * @brief Standard priority levels for the scheduler.
 */
#define __TT_SCHED_URGENT__  -20
#define __TT_SCHED_HIGH__    -10
#define __TT_SCHED_NORMAL__   0
#define __TT_SCHED_LAZY__     19

/**
 * @brief Priority range limits.
 */
#define __TT_PRIO_MIN        -20
#define __TT_PRIO_MAX         19

/**
 * @brief Convert nice value to priority.
 */
int ttak_nice_to_prio(int nice);

/**
 * @brief Shuffles an array of nice values.
 * @param nices Array of nice values.
 * @param count Number of elements.
 */
void ttak_shuffle_by_nice(int *nices, size_t count);

/**
 * @brief Locks the priority to a safe user range (0 to 19).
 * @param nice The nice value to lock.
 * @return The locked nice value.
 */
int ttak_lock_priority(int nice);

#endif // TTAK_PRIORITY_NICE_H
