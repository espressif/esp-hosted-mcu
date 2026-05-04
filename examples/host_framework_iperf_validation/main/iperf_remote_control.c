/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "esp_log.h"

#include "iperf.h"
#include "iperf_remote_control.h"

#define IPERF_CONTROL_PORT 22336
#define IPERF_CONTROL_BACKLOG 2
#define IPERF_CONTROL_MAX_CMD_LEN 128
#define IPERF_CONTROL_MAX_ARGS 8
#define IPERF_CONTROL_TASK_NAME "iperf_ctrl"
#define IPERF_CONTROL_TASK_STACK 4096
#define IPERF_CONTROL_TASK_PRIORITY 5
#define IPERF_CONTROL_CLIENT_TIMEOUT_SEC 5

static const char *TAG = "iperf_ctrl";
static bool s_control_task_started;

static bool parse_u16_arg(const char *text, uint16_t *value)
{
    char *end = NULL;
    unsigned long parsed = 0;

    if (!text || !value) {
        return false;
    }

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT16_MAX || parsed == 0) {
        return false;
    }

    *value = (uint16_t)parsed;
    return true;
}

static bool parse_u32_arg(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed = 0;

    if (!text || !value) {
        return false;
    }

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX || parsed == 0) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static bool parse_i32_arg(const char *text, int32_t *value)
{
    char *end = NULL;
    long parsed = 0;

    if (!text || !value) {
        return false;
    }

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }

    *value = (int32_t)parsed;
    return true;
}

static void init_cfg_defaults(iperf_cfg_t *cfg, bool is_udp, bool is_server,
        uint16_t port, uint32_t interval, uint32_t time, int32_t bandwidth)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->flag = is_server ? IPERF_FLAG_SERVER : IPERF_FLAG_CLIENT;
    cfg->flag |= is_udp ? IPERF_FLAG_UDP : IPERF_FLAG_TCP;
    cfg->type = IPERF_IP_TYPE_IPV4;
    cfg->format = MBITS_PER_SEC;
    cfg->interval = interval;
    cfg->time = time > interval ? time : interval;
    cfg->bw_lim = bandwidth > 0 ? bandwidth : IPERF_DEFAULT_NO_BW_LIMIT;

    if (is_server) {
        cfg->sport = port;
        cfg->dport = IPERF_DEFAULT_PORT;
    } else {
        cfg->sport = IPERF_DEFAULT_PORT;
        cfg->dport = port;
    }
}

static char *trim_command(char *command)
{
    char *start = command;
    char *end = NULL;

    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (*start == '\0') {
        return start;
    }

    end = start + strlen(start) - 1;
    while (end >= start && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return start;
}

static int split_command(char *command, char *argv[], int max_args)
{
    int argc = 0;
    char *token = strtok(command, " \t");

    while (token != NULL && argc < max_args) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }

    return argc;
}

static esp_err_t start_server(bool is_udp, uint16_t port, uint32_t interval,
        uint32_t time, char *response, size_t response_size)
{
    iperf_cfg_t cfg;
    esp_err_t err;

    init_cfg_defaults(&cfg, is_udp, true, port, interval, time, IPERF_DEFAULT_NO_BW_LIMIT);
    err = iperf_start(&cfg);
    if (err != ESP_OK) {
        snprintf(response, response_size, "ERR failed to start iperf server\n");
        return err;
    }

    snprintf(response, response_size,
            "OK started %s-server port=%u interval=%" PRIu32 " time=%" PRIu32 "\n",
            is_udp ? "udp" : "tcp", port, interval, cfg.time);
    return ESP_OK;
}

static esp_err_t start_client(bool is_udp, const char *host, uint16_t port,
        uint32_t interval, uint32_t time, int32_t bandwidth,
        char *response, size_t response_size)
{
    iperf_cfg_t cfg;
    esp_err_t err;
    struct in_addr addr;

    if (inet_aton(host, &addr) == 0) {
        snprintf(response, response_size, "ERR invalid host\n");
        return ESP_ERR_INVALID_ARG;
    }

    init_cfg_defaults(&cfg, is_udp, false, port, interval, time, bandwidth);
    cfg.destination_ip4 = addr.s_addr;

    err = iperf_start(&cfg);
    if (err != ESP_OK) {
        snprintf(response, response_size, "ERR failed to start iperf client\n");
        return err;
    }

    snprintf(response, response_size,
            "OK started %s-client host=%s port=%u interval=%" PRIu32 " time=%" PRIu32 " bw=%" PRId32 "\n",
            is_udp ? "udp" : "tcp", host, port, interval, cfg.time, cfg.bw_lim);
    return ESP_OK;
}

static esp_err_t handle_start_command(int argc, char *argv[], char *response, size_t response_size)
{
    uint16_t port = IPERF_DEFAULT_PORT;
    uint32_t interval = IPERF_DEFAULT_INTERVAL;
    uint32_t time = IPERF_DEFAULT_TIME;
    int32_t bandwidth = IPERF_DEFAULT_NO_BW_LIMIT;

    if (argc < 5) {
        snprintf(response, response_size, "ERR usage: START <tcp|udp>-<server|client> ...\n");
        return ESP_ERR_INVALID_ARG;
    }

    if (strcasecmp(argv[1], "tcp-server") == 0) {
        if (!parse_u16_arg(argv[2], &port) || !parse_u32_arg(argv[3], &interval) ||
                !parse_u32_arg(argv[4], &time)) {
            snprintf(response, response_size, "ERR invalid port/interval/time\n");
            return ESP_ERR_INVALID_ARG;
        }
        return start_server(false, port, interval, time, response, response_size);
    }

    if (strcasecmp(argv[1], "udp-server") == 0) {
        if (!parse_u16_arg(argv[2], &port) || !parse_u32_arg(argv[3], &interval) ||
                !parse_u32_arg(argv[4], &time)) {
            snprintf(response, response_size, "ERR invalid port/interval/time\n");
            return ESP_ERR_INVALID_ARG;
        }
        return start_server(true, port, interval, time, response, response_size);
    }

    if (strcasecmp(argv[1], "tcp-client") == 0) {
        if (argc != 6 || !parse_u16_arg(argv[3], &port) || !parse_u32_arg(argv[4], &interval) ||
                !parse_u32_arg(argv[5], &time)) {
            snprintf(response, response_size,
                    "ERR usage: START tcp-client <host> <port> <interval> <time>\n");
            return ESP_ERR_INVALID_ARG;
        }
        return start_client(false, argv[2], port, interval, time, bandwidth, response, response_size);
    }

    if (strcasecmp(argv[1], "udp-client") == 0) {
        if (argc != 7 || !parse_u16_arg(argv[3], &port) || !parse_u32_arg(argv[4], &interval) ||
                !parse_u32_arg(argv[5], &time) || !parse_i32_arg(argv[6], &bandwidth) || bandwidth <= 0) {
            snprintf(response, response_size,
                    "ERR usage: START udp-client <host> <port> <interval> <time> <bandwidth_mbps>\n");
            return ESP_ERR_INVALID_ARG;
        }
        return start_client(true, argv[2], port, interval, time, bandwidth, response, response_size);
    }

    snprintf(response, response_size, "ERR unsupported mode\n");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t handle_command(char *command, char *response, size_t response_size)
{
    char *argv[IPERF_CONTROL_MAX_ARGS] = { 0 };
    int argc = 0;
    char *trimmed = trim_command(command);

    if (*trimmed == '\0') {
        snprintf(response, response_size, "ERR empty command\n");
        return ESP_ERR_INVALID_ARG;
    }

    if (strcasecmp(trimmed, "PING") == 0) {
        snprintf(response, response_size, "OK pong\n");
        return ESP_OK;
    }

    if (strcasecmp(trimmed, "STATUS") == 0) {
        snprintf(response, response_size, "OK %s\n", g_iperf_is_running ? "running" : "idle");
        return ESP_OK;
    }

    if (strcasecmp(trimmed, "STOP") == 0) {
        iperf_stop();
        snprintf(response, response_size, "OK stop requested\n");
        return ESP_OK;
    }

    argc = split_command(trimmed, argv, IPERF_CONTROL_MAX_ARGS);
    if (argc == 0) {
        snprintf(response, response_size, "ERR empty command\n");
        return ESP_ERR_INVALID_ARG;
    }

    if (strcasecmp(argv[0], "START") != 0) {
        snprintf(response, response_size, "ERR unsupported command\n");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (g_iperf_is_running) {
        snprintf(response, response_size, "ERR iperf busy\n");
        return ESP_ERR_INVALID_STATE;
    }

    return handle_start_command(argc, argv, response, response_size);
}

static void handle_client(int client_socket)
{
    char rx_buffer[IPERF_CONTROL_MAX_CMD_LEN] = { 0 };
    char tx_buffer[IPERF_CONTROL_MAX_CMD_LEN] = { 0 };
    struct timeval timeout = {
        .tv_sec = IPERF_CONTROL_CLIENT_TIMEOUT_SEC,
        .tv_usec = 0,
    };
    int received;

    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    received = recv(client_socket, rx_buffer, sizeof(rx_buffer) - 1, 0);

    if (received <= 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            snprintf(tx_buffer, sizeof(tx_buffer), "ERR command timeout\n");
        } else {
            snprintf(tx_buffer, sizeof(tx_buffer), "ERR failed to read command\n");
        }
    } else {
        rx_buffer[received] = '\0';
        ESP_LOGI(TAG, "command: %s", rx_buffer);
        handle_command(rx_buffer, tx_buffer, sizeof(tx_buffer));
    }

    send(client_socket, tx_buffer, strlen(tx_buffer), 0);
    shutdown(client_socket, 0);
    close(client_socket);
}

static void iperf_remote_control_task(void *arg)
{
    struct sockaddr_in listen_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(IPERF_CONTROL_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    while (true) {
        int listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        int reuse_addr = 1;

        if (listen_socket < 0) {
            ESP_LOGE(TAG, "socket failed: errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

        if (bind(listen_socket, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) != 0) {
            ESP_LOGE(TAG, "bind failed: errno=%d", errno);
            close(listen_socket);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (listen(listen_socket, IPERF_CONTROL_BACKLOG) != 0) {
            ESP_LOGE(TAG, "listen failed: errno=%d", errno);
            close(listen_socket);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "remote control ready on port %d", IPERF_CONTROL_PORT);

        while (true) {
            int client_socket = accept(listen_socket, NULL, NULL);
            if (client_socket < 0) {
                ESP_LOGW(TAG, "accept failed: errno=%d", errno);
                break;
            }
            handle_client(client_socket);
        }

        close(listen_socket);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t iperf_remote_control_start(void)
{
    BaseType_t task_created;

    if (s_control_task_started) {
        return ESP_OK;
    }

    task_created = xTaskCreate(iperf_remote_control_task, IPERF_CONTROL_TASK_NAME,
            IPERF_CONTROL_TASK_STACK, NULL, IPERF_CONTROL_TASK_PRIORITY, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "failed to create remote control task");
        return ESP_FAIL;
    }

    s_control_task_started = true;
    return ESP_OK;
}
