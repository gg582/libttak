#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include "../internal/app_types.h"
#include "hwinfo.h"

/* Using plain HTTP to bypass all SSL/TLS handshake issues (Code 35) */
#define PRIMENET_API_URL "http://v5.primenet.org/api/v1/report"

/**
 * @brief Generates a unique ComputerID for the GIMPS network.
 */
void generate_computer_id(char *buf, size_t len) {
    char mid[33] = {0};
    int fd = open("/etc/machine-id", O_RDONLY);
    if (fd < 0) {
        snprintf(buf, len, "proj-ttak-yjlee-node");
        return;
    }
    if (read(fd, mid, 8) <= 0) {
        snprintf(buf, len, "yunjin");
    } else {
        mid[8] = '\0';
        snprintf(buf, len, "proj-ttak-yjlee-%s", mid);
    }
    close(fd);
}

/**
 * @brief Reports results to PrimeNet using system curl via plain HTTP.
 */
int report_to_gimps(app_state_t *state, const gimps_result_t *res, const ttak_node_telemetry_t *telemetry) {
    if (!state || !res) return -1;
    (void)telemetry;

    char json_payload[1024];
    snprintf(json_payload, sizeof(json_payload),
             "{\"User\":\"%s\",\"ComputerID\":\"%s\",\"Software\":\"TTAK-v1.0\","
             "\"Result\":{\"p\":%u,\"Residue\":\"0x%016" PRIx64 "\",\"is_prime\":%s}}",
             state->userid, state->computerid,
             res->p, (uint64_t)res->residue, res->is_prime ? "true" : "false");

    /* * Executing curl command with plain HTTP.
     * --http1.1: Forces version 1.1 to simplify the connection.
     * --silent: Prevents outputting progress meter.
     */
    char command[2048];
    snprintf(command, sizeof(command),
             "curl --silent --http1.1 "
             "-X POST %s "
             "-H \"Content-Type: application/json\" "
             "-d '%s' "
             "--connect-timeout 10",
             PRIMENET_API_URL, json_payload);

    int ret = system(command);
    
    if (ret == 0) {
        printf("[NETWORK] p:%u reported successfully via HTTP.\n", res->p);
        return 0;
    } else {
        /* If this fails with HTTP, the server likely enforces HTTPS redirection. */
        fprintf(stderr, "[NETWORK ERROR] p:%u -> Request failed (Status: %d)\n", res->p, ret);
        return -1;
    }
}
