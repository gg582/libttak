# Development History

## 2026-01-30

### Initial Skeleton Creation & Build Stabilization
- Established naming convention: `ttak_<module>_<func>` and `ttak_<module>_<type_t>`.
- **Memory Management (`mem`):**
    - Created basic structure for `ttak_mem_alloc`, `ttak_mem_free`, `ttak_mem_realloc`.
    - Restored original detailed usage comments in `include/mem/mem.h`.
- **Async Tasks (`async/task`):**
    - Defined `ttak_task_t` and basic lifecycle functions.
- **Thread Pool (`thread/pool`):**
    - Defined `ttak_thread_pool_t` skeleton.
- **Synchronization (`sync`):**
    - Created `pthread` wrappers for mutex and condition variables with `ttak_` prefix.
- **Hash Map (`ht`):**
    - Fixed naming inconsistencies and syntax errors in `src/ht/map.c`.
    - Implemented `gen_hash` in `src/ht/hash.c`.
    - Unified function names to use `ttak_` prefix in `include/ht/map.h`.
    - Fixed missing `#include <stddef.h>` and broken `#ifndef` in `include/ht/hash.h`.
    - **Restored Shortcuts:** Re-added `#define ttak_... tt_...` macros in `include/ht/map.h` to support original short names while maintaining naming convention.
    - **Restored Comments:** Fully restored detailed comments in `src/ht/map.c` regarding memory initialization, load factor, and resizing logic.
- **Atomic (`atomic`):**
    - Fixed `ttak_generic_func_t` typedef syntax.
    - Implemented skeletal `atomic_function_execute`.
- **Build System:**
    - Verified successful compilation of `lib/libttak.a` using `make`.

### Key Diffs Overview
- **ht/map.h:** Added shortcut macros (`ttak_create_map` -> `tt_create_map`, etc.) to align with original usage.
- **ht/map.c:** Reverted comments to original detailed versions. Renamed functions to `ttak_` to match header declarations (which then expand to `tt_` symbols via macros).
