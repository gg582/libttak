#include <ttak/priority/nice.h>

int ttak_nice_to_prio(int nice) {
    if (nice < __TT_PRIO_MIN) return __TT_PRIO_MIN;
    if (nice > __TT_PRIO_MAX) return __TT_PRIO_MAX;
    return nice;
}
