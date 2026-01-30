#ifndef TTAK_PRIORITY_NICE_H
#define TTAK_PRIORITY_NICE_H

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

#endif // TTAK_PRIORITY_NICE_H
