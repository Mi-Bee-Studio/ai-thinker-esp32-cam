#include "ftp_client.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

static const char *TAG = "ftp";

/* ── Socket helpers ──────────────────────────────────────────────── */

static esp_err_t ftp_read_response(int sock, char *buf, size_t buf_len, int *code)
{
    size_t total = 0;

    while (total < buf_len - 1) {
        int n = recv(sock, buf + total, 1, 0);
        if (n <= 0) {
            ESP_LOGE(TAG, "recv failed (errno %d)", errno);
            return ESP_FAIL;
        }
        total += n;

        /* Complete line: look for \r\n */
        if (total >= 2 && buf[total - 2] == '\r' && buf[total - 1] == '\n') {
            buf[total] = '\0';

            /* Multi-line response: "NNN-" prefix, keep reading */
            if (total >= 4 && buf[3] == '-') {
                ESP_LOGD(TAG, "<%s", buf);
                total = 0;
                continue;
            }

            ESP_LOGI(TAG, "< %s", buf);

            if (total >= 3 && code) {
                *code = (buf[0] - '0') * 100 + (buf[1] - '0') * 10 + (buf[2] - '0');
            }
            return ESP_OK;
        }
    }

    buf[total] = '\0';
    ESP_LOGW(TAG, "response buffer full: %s", buf);
    return ESP_FAIL;
}

static esp_err_t ftp_send_cmd(int sock, const char *cmd)
{
    ESP_LOGI(TAG, "> %s", cmd);
    int n = send(sock, cmd, strlen(cmd), 0);
    if (n < 0) {
        ESP_LOGE(TAG, "send failed (errno %d)", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t ftp_cmd_expect(int sock, const char *cmd, int expected)
{
    if (ftp_send_cmd(sock, cmd) != ESP_OK) {
        return ESP_FAIL;
    }
    int code = 0;
    char buf[256] = {0};
    if (ftp_read_response(sock, buf, sizeof(buf), &code) != ESP_OK) {
        return ESP_FAIL;
    }
    if (code != expected) {
        ESP_LOGE(TAG, "expected %d, got %d", expected, code);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ── PASV parser ─────────────────────────────────────────────────── */

static esp_err_t ftp_parse_pasv(const char *resp, char *ip, size_t ip_len, uint16_t *port)
{
    const char *p = strchr(resp, '(');
    if (!p) return ESP_FAIL;
    p++;

    int h1, h2, h3, h4, p1, p2;
    if (sscanf(p, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        ESP_LOGE(TAG, "PASV parse failed: %s", resp);
        return ESP_FAIL;
    }

    snprintf(ip, ip_len, "%d.%d.%d.%d", h1, h2, h3, h4);
    *port = (uint16_t)(p1 * 256 + p2);
    return ESP_OK;
}

/* ── Upload ──────────────────────────────────────────────────────── */

esp_err_t ftp_upload(const char *host, uint16_t port,
                     const char *user, const char *pass,
                     const char *remote_path, const char *local_path)
{
    esp_err_t ret = ESP_FAIL;
    int ctrl_sock = -1;
    int data_sock = -1;

    /* DNS resolve */
    struct hostent *he = gethostbyname(host);
    if (!he) {
        ESP_LOGE(TAG, "DNS lookup failed for %s", host);
        return ESP_FAIL;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(port),
    };
    memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));

    /* Open control socket */
    ctrl_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ctrl_sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        return ESP_FAIL;
    }

    /* 10s recv timeout */
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(ctrl_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(ctrl_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "connect to %s:%u failed (errno %d)", host, port, errno);
        close(ctrl_sock);
        return ESP_FAIL;
    }

    /* Open local file */
    FILE *fp = fopen(local_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "cannot open %s", local_path);
        close(ctrl_sock);
        return ESP_FAIL;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char buf[512];

    /* 220 welcome */
    int code = 0;
    if (ftp_read_response(ctrl_sock, buf, sizeof(buf), &code) != ESP_OK || code != 220) {
        ESP_LOGE(TAG, "bad welcome (code %d)", code);
        goto cleanup;
    }

    /* USER → 331 */
    {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "USER %s\r\n", user);
        if (ftp_cmd_expect(ctrl_sock, cmd, 331) != ESP_OK) goto cleanup;
    }

    /* PASS → 230 */
    {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "PASS %s\r\n", pass);
        if (ftp_cmd_expect(ctrl_sock, cmd, 230) != ESP_OK) goto cleanup;
    }

    /* TYPE I → 200 */
    if (ftp_cmd_expect(ctrl_sock, "TYPE I\r\n", 200) != ESP_OK) goto cleanup;

    /* PASV → 227 */
    ftp_send_cmd(ctrl_sock, "PASV\r\n");
    ftp_read_response(ctrl_sock, buf, sizeof(buf), &code);
    if (code != 227) {
        ESP_LOGE(TAG, "PASV failed (code %d)", code);
        goto cleanup;
    }

    /* Parse data endpoint */
    {
        char data_ip[48];
        uint16_t data_port;
        if (ftp_parse_pasv(buf, data_ip, sizeof(data_ip), &data_port) != ESP_OK) {
            goto cleanup;
        }

        /* Connect data socket */
        data_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (data_sock < 0) goto cleanup;

        struct sockaddr_in daddr = {
            .sin_family = AF_INET,
            .sin_port   = htons(data_port),
            .sin_addr.s_addr = inet_addr(data_ip),
        };

        if (connect(data_sock, (struct sockaddr *)&daddr, sizeof(daddr)) != 0) {
            ESP_LOGE(TAG, "data connect to %s:%u failed", data_ip, data_port);
            close(data_sock);
            data_sock = -1;
            goto cleanup;
        }
    }

    /* STOR on control channel → 150 */
    {
        char cmd[280];
        snprintf(cmd, sizeof(cmd), "STOR %s\r\n", remote_path);
        if (ftp_cmd_expect(ctrl_sock, cmd, 150) != ESP_OK) {
            if (data_sock >= 0) { close(data_sock); data_sock = -1; }
            goto cleanup;
        }
    }

    /* Transfer data in 4KB chunks */
    {
        uint8_t *chunk = malloc(4096);
        if (!chunk) {
            if (data_sock >= 0) { close(data_sock); data_sock = -1; }
            goto cleanup;
        }

        size_t total_sent = 0;
        while (total_sent < (size_t)file_size) {
            size_t to_read = 4096;
            if (to_read > (size_t)file_size - total_sent) {
                to_read = (size_t)file_size - total_sent;
            }
            size_t nread = fread(chunk, 1, to_read, fp);
            if (nread == 0) break;

            int nsent = send(data_sock, chunk, nread, 0);
            if (nsent < 0) {
                ESP_LOGE(TAG, "data send failed (errno %d)", errno);
                free(chunk);
                close(data_sock);
                data_sock = -1;
                goto cleanup;
            }
            total_sent += nsent;
        }

        free(chunk);
    }

    close(data_sock);
    data_sock = -1;

    /* Expect 226 transfer complete */
    ftp_read_response(ctrl_sock, buf, sizeof(buf), &code);
    if (code != 226) {
        ESP_LOGW(TAG, "expected 226, got %d", code);
    }

    ESP_LOGI(TAG, "uploaded %s (%ld bytes)", remote_path, file_size);
    ret = ESP_OK;

cleanup:
    /* QUIT */
    if (ctrl_sock >= 0) {
        ftp_send_cmd(ctrl_sock, "QUIT\r\n");
        char quit_buf[64];
        ftp_read_response(ctrl_sock, quit_buf, sizeof(quit_buf), NULL);
        close(ctrl_sock);
    }
    if (data_sock >= 0) close(data_sock);
    fclose(fp);
    return ret;
}
