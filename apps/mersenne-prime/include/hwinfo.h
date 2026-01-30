#ifndef TTAK_MERSENNE_HWINFO_H
#define TTAK_MERSENNE_HWINFO_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char hostname[64];
    char os_name[128];
    char kernel[128];
    char architecture[32];
    char cpu_model[128];
    char cpu_flags[256];
    char vendor_string[64];
    char optimized_features[128];
    char environment[192];
    uint32_t logical_cores;
    uint32_t physical_cores;
    uint64_t cpu_freq_khz;
    uint64_t cache_l1_kb;
    uint64_t cache_l2_kb;
    uint64_t cache_l3_kb;
    uint64_t total_mem_kb;
    uint64_t avail_mem_kb;
    double load_avg[3];
} ttak_hw_spec_t;

typedef struct {
    ttak_hw_spec_t spec;
    double uptime_seconds;
    double ops_per_second;
    uint64_t total_ops;
    uint32_t active_workers;
    uint32_t exponent_in_progress;
    uint64_t latest_residue;
    bool residue_is_zero;
    char residual_snapshot[128];
    uint64_t iteration_time_ms;
} ttak_node_telemetry_t;

bool ttak_collect_hw_spec(ttak_hw_spec_t *spec);
double ttak_query_uptime_seconds(void);
void ttak_build_node_telemetry(ttak_node_telemetry_t *telemetry,
                               const ttak_hw_spec_t *spec,
                               double ops_per_sec,
                               double interval_ms,
                               uint64_t total_ops,
                               uint32_t active_workers,
                               uint32_t exponent,
                               uint64_t residue,
                               bool residue_is_zero);

#endif // TTAK_MERSENNE_HWINFO_H
