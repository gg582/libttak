#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <fcntl.h>
#include "../internal/app_types.h"

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
    read(fd, machine_id, 8);
    close(fd);
    machine_id[8] = '\0';
    snprintf(buf, len, "proj-ttak-yjlee-%s", machine_id);
}

/**
 * @brief Reports a result to GIMPS via PrimeNet v5 API.
 */
int report_to_gimps(app_state_t *state, gimps_result_t *res) {
    CURL *curl;
    CURLcode response;
    int success = -1;

    curl = curl_easy_init();
    if (!curl) return -1;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char json_payload[512];
    snprintf(json_payload, sizeof(json_payload),
             "{\"computerid\": \"%s\", \"userid\": \"%s\", \"exponent\": %u, "
             "\"residue\": \"0x%lx\", \"is_prime\": %s, \"software\": \"%s\"}",
             state->computerid, state->userid, res->p, res->residue,
             res->is_prime ? "true" : "false", SOFTWARE_ID);

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