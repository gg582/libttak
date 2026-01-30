#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include "../internal/app_types.h"
#include "hwinfo.h"

#define PRIMENET_API_URL "https://v5.primenet.org/api/v1/report"
#define SOFTWARE_ID "TTAK-Engine-v1.0"

/**
 * @brief Generates the ComputerID based on /etc/machine-id.
 */
void generate_computer_id(char *buf, size_t len) {
    char machine_id[33] = {0};
    int fd = open("/etc/machine-id", O_RDONLY);
    if (fd < 0) {
        snprintf(buf, len, "proj-ttak-yjlee-unknown");
        return;
    }
    int res = read(fd, machine_id, 8);
    if(res <= 0) {
        fprintf(stderr, "[ERROR] no machine name");
        snprintf(buf, len, "yunjin");
        close(fd);
        return;
    }
    close(fd);
    machine_id[8] = '\0';
    snprintf(buf, len, "proj-ttak-yjlee-%s", machine_id);
}

/**
 * @brief Reports a result to GIMPS via PrimeNet v5 API.
 */
static void json_sanitize(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t di = 0;
    for (size_t i = 0; src[i] != '\0' && di + 1 < dst_len; ++i) {
        char c = src[i];
        if (c == '"' || c == '\\') c = ' ';
        dst[di++] = c;
    }
    dst[di] = '\0';
}

int report_to_gimps(app_state_t *state, const gimps_result_t *res, const ttak_node_telemetry_t *telemetry) {
    if (!state || !res) return -1;
    CURL *curl;
    CURLcode response;
    int success = -1;

    curl = curl_easy_init();
    if (!curl) return -1;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    ttak_node_telemetry_t empty = {0};
    if (!telemetry) telemetry = &empty;

    char hostname[64], os_name[128], kernel[128], arch[32], cpu_model[128], cpu_flags[256];
    char residual_snapshot[128], vendor_string[64], optimized[128], environment[192];
    char computerid[64], userid[64];
    json_sanitize(hostname, sizeof(hostname), telemetry->spec.hostname);
    json_sanitize(os_name, sizeof(os_name), telemetry->spec.os_name);
    json_sanitize(kernel, sizeof(kernel), telemetry->spec.kernel);
    json_sanitize(arch, sizeof(arch), telemetry->spec.architecture);
    json_sanitize(cpu_model, sizeof(cpu_model), telemetry->spec.cpu_model);
    json_sanitize(cpu_flags, sizeof(cpu_flags), telemetry->spec.cpu_flags);
    json_sanitize(residual_snapshot, sizeof(residual_snapshot), telemetry->residual_snapshot);
    json_sanitize(vendor_string, sizeof(vendor_string), telemetry->spec.vendor_string);
    json_sanitize(optimized, sizeof(optimized), telemetry->spec.optimized_features);
    json_sanitize(environment, sizeof(environment), telemetry->spec.environment);
    json_sanitize(computerid, sizeof(computerid), state->computerid);
    json_sanitize(userid, sizeof(userid), state->userid);

    char cpu_summary[256];
    double freq_mhz = telemetry->spec.cpu_freq_khz ? (telemetry->spec.cpu_freq_khz / 1000.0) : 0.0;
    snprintf(cpu_summary, sizeof(cpu_summary), "%s (%u logical / %u physical @ %.0f MHz)",
             cpu_model[0] ? cpu_model : "Unknown CPU",
             telemetry->spec.logical_cores,
             telemetry->spec.physical_cores,
             freq_mhz);
    json_sanitize(cpu_summary, sizeof(cpu_summary), cpu_summary);

    char cache_summary[128];
    snprintf(cache_summary, sizeof(cache_summary), "L1:%" PRIu64 "KB L2:%" PRIu64 "KB L3:%" PRIu64 "KB",
             telemetry->spec.cache_l1_kb,
             telemetry->spec.cache_l2_kb,
             telemetry->spec.cache_l3_kb);
    json_sanitize(cache_summary, sizeof(cache_summary), cache_summary);

    double mem_usage = 0.0;
    if (telemetry->spec.total_mem_kb > 0) {
        double used = (double)(telemetry->spec.total_mem_kb - telemetry->spec.avail_mem_kb);
        mem_usage = used / (double)telemetry->spec.total_mem_kb;
    }

    char json_payload[4096];
    snprintf(json_payload, sizeof(json_payload),
             "{"
             "\"User\":\"%s\","
             "\"ComputerID\":\"%s\","
             "\"Vendor\":\"%s\","
             "\"Software\":\"%s\","
             "\"Result\":{"
                 "\"p\":%u,"
                 "\"Residue\":\"0x%016" PRIx64 "\","
                 "\"TimeMS\":%" PRIu64 ","
                 "\"is_prime\":%s"
             "},"
             "\"Telemetry\":{"
                 "\"uptime_sec\":%.2f,"
                 "\"ops_per_sec\":%.2f,"
                 "\"total_ops\":%" PRIu64 ","
                 "\"active_workers\":%u,"
                 "\"exponent\":%u,"
                 "\"residue_zero\":%s,"
                 "\"residual_snapshot\":\"%s\""
             "},"
             "\"Hardware\":{"
                 "\"Hostname\":\"%s\","
                 "\"CPU\":\"%s\","
                 "\"L3Cache\":\"%" PRIu64 "KB\","
                 "\"Optimized\":\"%s\","
                 "\"Environment\":\"%s\""
             "},"
             "\"HardwareDetail\":{"
                 "\"kernel\":\"%s\","
                 "\"arch\":\"%s\","
                 "\"cpu_flags\":\"%s\","
                 "\"logical_cores\":%u,"
                 "\"physical_cores\":%u,"
                 "\"cpu_freq_khz\":%" PRIu64 ","
                 "\"cache_summary\":\"%s\","
                 "\"total_mem_kb\":%" PRIu64 ","
                 "\"avail_mem_kb\":%" PRIu64 ","
                 "\"memory_utilization\":%.4f,"
                 "\"loadavg1\":%.2f,"
                 "\"loadavg5\":%.2f,"
                 "\"loadavg15\":%.2f"
             "}"
             "}",
             userid[0] ? userid : "anonymous",
             computerid[0] ? computerid : "unknown",
             vendor_string[0] ? vendor_string : "libttak/glibc(Intel N150)",
             SOFTWARE_ID,
             res->p,
             (uint64_t)res->residue,
             telemetry->iteration_time_ms,
             res->is_prime ? "true" : "false",
             telemetry->uptime_seconds,
             telemetry->ops_per_second,
             telemetry->total_ops,
             telemetry->active_workers,
             telemetry->exponent_in_progress,
             telemetry->residue_is_zero ? "true" : "false",
             residual_snapshot,
             hostname,
             cpu_summary,
             telemetry->spec.cache_l3_kb,
             optimized[0] ? optimized : "AVX2, Montgomery-NTT",
             environment[0] ? environment : "unknown",
             kernel,
             arch,
             cpu_flags,
             telemetry->spec.logical_cores,
             telemetry->spec.physical_cores,
             telemetry->spec.cpu_freq_khz,
             cache_summary,
             telemetry->spec.total_mem_kb,
             telemetry->spec.avail_mem_kb,
             mem_usage,
             telemetry->spec.load_avg[0],
             telemetry->spec.load_avg[1],
             telemetry->spec.load_avg[2]);

    curl_easy_setopt(curl, CURLOPT_URL, PRIMENET_API_URL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10s timeout for N150 reliability

    response = curl_easy_perform(curl);
    if (response == CURLE_OK) {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if (response_code >= 200 && response_code < 300) {
            success = 0;
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return success;
}
