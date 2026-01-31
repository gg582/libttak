#include <ttak/priority/nice.h>

int ttak_nice_to_prio(int nice) {
    if (nice < __TT_PRIO_MIN) return __TT_PRIO_MIN;
    if (nice > __TT_PRIO_MAX) return __TT_PRIO_MAX;
    return nice;
}

int ttak_compare_nice(int nice1, int nice2) {
    return nice1 - nice2;
}

#include <stdlib.h>

void ttak_shuffle_by_nice(int *nices, size_t count) {
    if (!nices || count <= 1) return;
    for (size_t i = 0; i < count - 1; i++) {
        size_t j = i + rand() / (RAND_MAX / (count - i) + 1);
        int t = nices[j];
        nices[j] = nices[i];
        nices[i] = t;
    }
}

int ttak_lock_priority(int nice) {
    if (nice < __TT_SCHED_NORMAL__) return __TT_SCHED_NORMAL__;
    if (nice > __TT_PRIO_MAX) return __TT_PRIO_MAX;
    return nice;
}
