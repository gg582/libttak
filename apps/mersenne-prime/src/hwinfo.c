#define _GNU_SOURCE
#include "hwinfo.h"
#include <ctype.h>
#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

static void trim_newline(char *str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[--len] = '\0';
    }
}

static void read_os_release(char *out, size_t len) {
    FILE *fp = fopen("/etc/os-release", "r");
    if (!fp) {
        snprintf(out, len, "Unknown Linux");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
            char *value = strchr(line, '=');
            if (value) {
                value++;
                size_t vlen = strlen(value);
                if (vlen > 0 && value[0] == '"' && value[vlen - 1] == '"') {
                    value[vlen - 1] = '\0';
                    value++;
                }
                trim_newline(value);
                snprintf(out, len, "%s", value);
                fclose(fp);
                return;
            }
        }
    }

    fclose(fp);
    snprintf(out, len, "Unknown Linux");
}

static uint64_t read_cache_sysfs(int index) {
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu0/cache/index%d/size", index);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    char buf[32];
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    trim_newline(buf);

    char *end = buf;
    while (*end && !isalpha((unsigned char)*end)) end++;
    uint64_t value = strtoull(buf, NULL, 10);
    if (end && (*end == 'M' || *end == 'm')) {
        value *= 1024ULL;
    }
    return value;
}

static void read_cpuinfo(ttak_hw_spec_t *spec) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return;

    char line[512];
    uint32_t logical = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "processor", 9) == 0) {
            logical++;
        } else if (strncmp(line, "model name", 10) == 0 && spec->cpu_model[0] == '\0') {
            char *colon = strchr(line, ':');
            if (colon) {
                colon++;
                while (*colon == ' ' || *colon == '\t') colon++;
                trim_newline(colon);
                snprintf(spec->cpu_model, sizeof(spec->cpu_model), "%s", colon);
            }
        } else if (strncmp(line, "cpu MHz", 7) == 0) {
            double mhz = atof(strchr(line, ':') + 1);
            spec->cpu_freq_khz = (uint64_t)(mhz * 1000.0);
        } else if (strncmp(line, "cpu cores", 9) == 0) {
            spec->physical_cores = (uint32_t)strtoul(strchr(line, ':') + 1, NULL, 10);
        } else if (strncmp(line, "flags", 5) == 0 && spec->cpu_flags[0] == '\0') {
            char *colon = strchr(line, ':');
            if (colon) {
                colon++;
                trim_newline(colon);
                snprintf(spec->cpu_flags, sizeof(spec->cpu_flags), "%s", colon);
            }
        }
    }
    fclose(fp);

    if (spec->logical_cores == 0) spec->logical_cores = logical;
    if (spec->cache_l1_kb == 0) {
        uint64_t l1d = read_cache_sysfs(0);
        uint64_t l1i = read_cache_sysfs(1);
        spec->cache_l1_kb = l1d + l1i;
    }
    if (spec->cache_l2_kb == 0) {
        spec->cache_l2_kb = read_cache_sysfs(2);
    }
    if (spec->cache_l3_kb == 0) {
        spec->cache_l3_kb = read_cache_sysfs(3);
    }
}

static void read_meminfo(ttak_hw_spec_t *spec) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            spec->total_mem_kb = strtoull(line + 9, NULL, 10);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            spec->avail_mem_kb = strtoull(line + 13, NULL, 10);
        }
    }
    fclose(fp);
}

bool ttak_collect_hw_spec(ttak_hw_spec_t *spec) {
    if (!spec) return false;
    memset(spec, 0, sizeof(*spec));

    if (gethostname(spec->hostname, sizeof(spec->hostname)) != 0) {
        snprintf(spec->hostname, sizeof(spec->hostname), "unknown-host");
    }

    read_os_release(spec->os_name, sizeof(spec->os_name));

    struct utsname uts;
    if (uname(&uts) == 0) {
        snprintf(spec->kernel, sizeof(spec->kernel), "%.63s %.63s", uts.sysname, uts.release);
        snprintf(spec->architecture, sizeof(spec->architecture), "%.31s", uts.machine);
    } else {
        snprintf(spec->kernel, sizeof(spec->kernel), "unknown");
        snprintf(spec->architecture, sizeof(spec->architecture), "unknown");
    }

    snprintf(spec->vendor_string, sizeof(spec->vendor_string), "libttak/glibc(Intel N150)");

    read_cpuinfo(spec);
    read_meminfo(spec);

    if (spec->logical_cores == 0) {
        long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        spec->logical_cores = (cpus > 0) ? (uint32_t)cpus : 1;
    }
    if (spec->physical_cores == 0) {
        spec->physical_cores = spec->logical_cores;
    }

    double load[3] = {0};
    if (getloadavg(load, 3) == 3) {
        memcpy(spec->load_avg, load, sizeof(load));
    }

    snprintf(spec->optimized_features, sizeof(spec->optimized_features), "AVX2, Montgomery-NTT");
    snprintf(spec->environment, sizeof(spec->environment), "%.80s / %.80s", spec->os_name, spec->kernel);

    return true;
}

double ttak_query_uptime_seconds(void) {
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) return 0.0;

    double uptime = 0.0;
    if (fscanf(fp, "%lf", &uptime) != 1) {
        uptime = 0.0;
    }
    fclose(fp);
    return uptime;
}

void ttak_build_node_telemetry(ttak_node_telemetry_t *telemetry,
                               const ttak_hw_spec_t *spec,
                               double ops_per_sec,
                               double interval_ms,
                               uint64_t total_ops,
                               uint32_t active_workers,
                               uint32_t exponent,
                               uint64_t residue,
                               bool residue_is_zero) {
    if (!telemetry || !spec) return;
    memset(telemetry, 0, sizeof(*telemetry));
    memcpy(&telemetry->spec, spec, sizeof(*spec));
    telemetry->uptime_seconds = ttak_query_uptime_seconds();
    telemetry->ops_per_second = ops_per_sec;
    telemetry->total_ops = total_ops;
    telemetry->active_workers = active_workers;
    telemetry->exponent_in_progress = exponent;
    telemetry->latest_residue = residue;
    telemetry->residue_is_zero = residue_is_zero;
    telemetry->iteration_time_ms = (interval_ms > 0.0) ? (uint64_t)interval_ms : 0;
    snprintf(telemetry->residual_snapshot, sizeof(telemetry->residual_snapshot),
             "p=%u ops=%" PRIu64 " residue=0x%016" PRIx64,
             exponent, total_ops, residue);
}
