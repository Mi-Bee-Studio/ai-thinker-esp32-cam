/**
 * @file mjpeg_streamer.c
 * @brief MJPEG real-time video streaming via independent TCP server on port 81.
 *
 * Captures camera frames via frame_broadcaster and pushes them as a
 * multipart/x-mixed-replace MJPEG stream to TCP clients.
 * Maximum 2 concurrent clients to limit PSRAM usage.
 * Target ~30 FPS with 8 KB chunked transfer.
 */

#include "mjpeg_streamer.h"
#include "frame_broadcaster.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <netinet/tcp.h>

static const char *TAG = "mjpeg_streamer";

/* ---------- Stream protocol constants ---------- */

#define BOUNDARY            "frame"
#define STREAM_BOUNDARY     "\r\n--" BOUNDARY "\r\n" \
                           "Content-Type: image/jpeg\r\n" \
                           "Content-Length: %zu\r\n\r\n"
#define CLOSING_BOUNDARY    "\r\n--" BOUNDARY "--\r\n"

#define MAX_STREAM_CLIENTS  1  /* Limit to 1 stream — leave WiFi bandwidth for httpd */
#define CHUNK_SIZE          8192
#define LISTEN_BACKLOG      2
#define CLIENT_TASK_STACK   4096
#define SEND_TIMEOUT_MS     15000
#define CLIENT_RECV_TIMEOUT 5

/* ---------- Module state ---------- */

static SemaphoreHandle_t s_mutex = NULL;
static int s_client_count = 0;

/* 客户端注册表（LRU 踢除）：本板仅 1 槽位，一个滞留连接就会让新页面永远 503。
 * 满员时 shutdown 旧连接，新连接（用户刚打开的页面）永远优先 */
typedef struct {
    int        fd;
    TickType_t since;
} mjpeg_client_slot_t;
static mjpeg_client_slot_t s_clients[MAX_STREAM_CLIENTS];
static TaskHandle_t s_listen_task = NULL;
static int s_listen_sock = -1;
static volatile bool s_running = false;

/* ---------- Forward declarations ---------- */

static void mjpeg_listen_task(void *arg);
static void mjpeg_client_task(void *arg);

/* ---------- Internal helpers ---------- */

static int get_client_count(void)
{
    int count = 0;
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        count = s_client_count;
        xSemaphoreGive(s_mutex);
    }
    return count;
}

/* ---------- Client task — serves one MJPEG stream connection ---------- */

static void mjpeg_client_task(void *arg)
{
    int client_sock = (int)(intptr_t)arg;

    /* Set send timeout so a stuck client does not hang the task */
    struct timeval tv = { .tv_sec = SEND_TIMEOUT_MS / 1000,
                          .tv_usec = (SEND_TIMEOUT_MS % 1000) * 1000 };
    setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Disable Nagle's algorithm — lower latency for small MJPEG part-headers */
    int flag = 1;
    setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    /* TCP keepalive：僵尸连接 ~30s 内被内核判死 */
    int ka = 1;
    setsockopt(client_sock, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka));
    int keep_idle = 10, keep_intvl = 5, keep_cnt = 3;
    setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle));
    setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPINTVL, &keep_intvl, sizeof(keep_intvl));
    setsockopt(client_sock, IPPROTO_TCP, TCP_KEEPCNT, &keep_cnt, sizeof(keep_cnt));

    /* Recv timeout — prevents zombie if client connects but never sends HTTP request */
    struct timeval rcvtv = { .tv_sec = CLIENT_RECV_TIMEOUT, .tv_usec = 0 };
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &rcvtv, sizeof(rcvtv));

    /* Read HTTP request (first 511 bytes is enough to validate) */
    char req_buf[512];
    int req_len = recv(client_sock, req_buf, sizeof(req_buf) - 1, 0);
    if (req_len <= 0) {
        ESP_LOGW(TAG, "Failed to read HTTP request (errno %d)", errno);
        close(client_sock);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_client_count--;
        xSemaphoreGive(s_mutex);
        vTaskDelete(NULL);
        return;
    }
    req_buf[req_len] = '\0';

    /* Validate: must be GET /stream (accept /stream?xxx too) */
    if (strncmp(req_buf, "GET /stream", 11) != 0) {
        ESP_LOGW(TAG, "Unexpected request: %.60s", req_buf);
        const char *resp = "HTTP/1.1 400 Bad Request\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n";
        send(client_sock, resp, strlen(resp), 0);
        close(client_sock);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_client_count--;
        xSemaphoreGive(s_mutex);
        vTaskDelete(NULL);
        return;
    }

    /* Send HTTP 200 + multipart/x-mixed-replace headers */
    char headers[512];
    int hdr_len = snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=" BOUNDARY "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-cache\r\n"
        "Pragma: no-cache\r\n"
        "Connection: close\r\n"
        "\r\n");

    if (send(client_sock, headers, hdr_len, 0) != hdr_len) {
        ESP_LOGW(TAG, "Failed to send response headers");
        close(client_sock);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_client_count--;
        xSemaphoreGive(s_mutex);
        vTaskDelete(NULL);
        return;
    }

    /* 登记注册表（LRU 依据）— 必须在请求校验通过之后 */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_STREAM_CLIENTS; i++) {
        if (s_clients[i].fd == 0) {
            s_clients[i].fd = client_sock;
            s_clients[i].since = xTaskGetTickCount();
            break;
        }
    }
    xSemaphoreGive(s_mutex);

ESP_LOGI(TAG, "Stream client started (total %d)", get_client_count());

    /* ---- Stream loop ------------------------------------------------- */
    char part_hdr[192];
    int capture_fails = 0;

    while (1) {
        /* Dead-client probe: non-blocking recv detects TCP FIN/RST immediately.
         * On a healthy one-way MJPEG stream, recv returns -1/EAGAIN (no data from
         * client, which is expected). On a dead connection it returns 0 (FIN) or
         * -1/ECONNRESET (RST), and we exit the stream loop to free the slot. */
        char probe;
        int pr = recv(client_sock, &probe, 1, MSG_DONTWAIT);
        if (pr == 0 || (pr < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            ESP_LOGW(TAG, "Client disconnected (probe rv=%d errno=%d)", pr, errno);
            break;
        }

        /* Capture frame with retry */
        camera_fb_t *fb = NULL;
        esp_err_t ret;
        int retries;
        for (retries = 0; retries < 3; retries++) {
            ret = frame_broker_get_copy(&fb, 2000);
            if (ret == ESP_OK) break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        if (ret != ESP_OK) {
            capture_fails++;
            if (capture_fails >= 10) {
                ESP_LOGW(TAG, "No frames after %d attempts, ending stream", capture_fails);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }
        capture_fails = 0;

        /* Build multipart part header */
        int hdrlen = snprintf(part_hdr, sizeof(part_hdr),
            STREAM_BOUNDARY, fb->len);

        /* Send part header */
        if (send(client_sock, part_hdr, hdrlen, 0) != hdrlen) {
            frame_broker_free(fb);
            break;
        }

        /* Send JPEG body in CHUNK_SIZE pieces */
        size_t remaining = fb->len;
        const uint8_t *ptr = fb->buf;
        bool send_ok = true;

        while (remaining > 0) {
            size_t chunk = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
            int sent = send(client_sock, (const char *)ptr, chunk, 0);
            if (sent <= 0) {
                send_ok = false;
                break;
            }
            ptr += sent;
            remaining -= sent;
        }

        frame_broker_free(fb);

        if (!send_ok) break;

        /* Trailing CRLF */
        if (send(client_sock, "\r\n", 2, 0) != 2) {
            break;
        }

        /* Frame-rate throttle — ~33 fps max */
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    /* Send closing boundary (best-effort) */
    send(client_sock, CLOSING_BOUNDARY, strlen(CLOSING_BOUNDARY), 0);

    close(client_sock);

/* 从注册表注销 */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_STREAM_CLIENTS; i++) {
        if (s_clients[i].fd == client_sock) {
            s_clients[i].fd = 0;
            break;
        }
    }
    s_client_count--;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Stream client disconnected (total %d)", get_client_count());
    vTaskDelete(NULL);
}

/* ---------- Listen task — accepts connections, spawns client tasks ---------- */

static void mjpeg_listen_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Listen task started");
    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(s_listen_sock,
                                 (struct sockaddr *)&client_addr,
                                 &addr_len);
        if (client_sock < 0) {
            if (errno == EINTR || errno == ECONNABORTED) {
                continue;
            }
            /* s_running check — if stopped, exit cleanly */
            if (!s_running) break;
            ESP_LOGE(TAG, "accept() failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* 发送超时兜底（2026-09-04 家族同步）：TCP 零窗口客户端的 send()
         * 会长期阻塞占住任务；10s 超时让其走断开清理。 */
        struct timeval snd_to = { .tv_sec = 10, .tv_usec = 0 };
        setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, &snd_to, sizeof(snd_to));

        /* 对端溯源（PIT-038）：重连风暴/锤击定位，accept 即记 IP */
        {
            char cip[INET_ADDRSTRLEN] = "?";
            inet_ntop(AF_INET, &client_addr.sin_addr, cip, sizeof(cip));
            ESP_LOGI(TAG, "Stream accept from %s (internal=%u)",
                     cip,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        }

        /* 防锤击护栏（PIT-038；2026-09-08 多 peer 化）：同 IP 两次接入间隔
         * <5s（NVR 类查看端 1-2s 重连风暴签名）→ 503 + 指数退避（10s 起
         * 步翻倍、封顶 5 分钟）；正常观众（SPA 被踢后 ~7s 自愈重连）不受
         * 影响。旧单 IP 追踪会被"锤子+观众交替接入"互洗（新 IP 接入即重置
         * 追踪对象，锤子借观众穿透踢线），故改 4 项每 IP 独立退避表（环
         * 替换）。拒绝静默计数（防日志风暴）。 */
        {
            enum { HAMMER_SLOTS = 4, HAMMER_MIN_GAP_MS = 5000 };
            static struct {
                struct in_addr peer;
                TickType_t last_accept;   /* 上次放行时刻 */
                TickType_t until;         /* 退避截止 */
                uint32_t backoff_ms;
                uint32_t rejected;
            } s_hammer[HAMMER_SLOTS];
            static int s_hammer_next;
            TickType_t now = xTaskGetTickCount();
            int h = -1;
            for (int i = 0; i < HAMMER_SLOTS; i++) {
                if (s_hammer[i].peer.s_addr == client_addr.sin_addr.s_addr) {
                    h = i;
                    break;
                }
            }
            if (h >= 0 && ((int32_t)(now - s_hammer[h].until) < 0 ||
                           (int32_t)(now - s_hammer[h].last_accept) <
                               pdMS_TO_TICKS(HAMMER_MIN_GAP_MS))) {
                s_hammer[h].until = now + pdMS_TO_TICKS(s_hammer[h].backoff_ms);
                s_hammer[h].backoff_ms = s_hammer[h].backoff_ms < 300000
                                             ? s_hammer[h].backoff_ms * 2 : 300000;
                if (++s_hammer[h].rejected % 50 == 1) {
                    char ipstr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ipstr, sizeof(ipstr));
                    ESP_LOGI(TAG, "Hammer guard: rejected %u from %s (backoff %us)",
                             (unsigned)s_hammer[h].rejected, ipstr,
                             s_hammer[h].backoff_ms / 1000);
                }
                const char *busy =
                    "HTTP/1.1 503 Service Unavailable\r\n"
                    "Content-Length: 23\r\n\r\nRetry after cooldown\r\n";
                send(client_sock, busy, strlen(busy), 0);
                close(client_sock);
                continue;
            }
            if (h < 0) {
                h = s_hammer_next;
                s_hammer_next = (s_hammer_next + 1) % HAMMER_SLOTS;
                s_hammer[h].rejected = 0;
                s_hammer[h].until = 0;
            }
            s_hammer[h].peer = client_addr.sin_addr;
            s_hammer[h].last_accept = now;
            s_hammer[h].backoff_ms = 10000;
        }

        /* 单槽位：已有连接（含滞留僵尸）一律让位给新连接（用户刚打开的页面） */
        bool slot_ready = false;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (s_client_count < MAX_STREAM_CLIENTS) {
            slot_ready = true;
        } else if (s_clients[0].fd != 0) {
            ESP_LOGW(TAG, "Slot busy — kicking old client fd=%d for newcomer", s_clients[0].fd);
            shutdown(s_clients[0].fd, SHUT_RDWR);
        }
        xSemaphoreGive(s_mutex);

        for (int wait = 0; !slot_ready && wait < 20; wait++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            slot_ready = (s_client_count < MAX_STREAM_CLIENTS);
            xSemaphoreGive(s_mutex);
        }
        if (!slot_ready) {
            ESP_LOGW(TAG, "Slot still busy after kick — rejecting with 503");
            const char *reject = "HTTP/1.1 503 Service Unavailable\r\n"
                                 "Content-Length: 25\r\n\r\nMax stream connections\r\n";
            send(client_sock, reject, strlen(reject), 0);
            close(client_sock);
            continue;
        }

        s_client_count++;
        xSemaphoreGive(s_mutex);

        /* Spawn a dedicated client task (Core 1, priority 2) */
        BaseType_t created = xTaskCreatePinnedToCore(
            mjpeg_client_task,
            "mjpeg_cli",
            CLIENT_TASK_STACK,
            (void *)(intptr_t)client_sock,
            2,
            NULL,
            1);

        if (created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create client task");
            close(client_sock);
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_client_count--;
            xSemaphoreGive(s_mutex);
        }
    }

    ESP_LOGI(TAG, "Listen task exiting");
    /* 自清全局句柄：stop() 轮询此标志判断任务已退，避免对已死任务
     * vTaskDelete 悬垂句柄（2026-09-07 OTA quiesce 必崩实锤）。 */
    s_listen_task = NULL;
    vTaskDelete(NULL);
}

/* ---------- Public API ---------- */

esp_err_t mjpeg_streamer_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    s_client_count = 0;
    ESP_LOGI(TAG, "MJPEG streamer initialized (max %d clients)", MAX_STREAM_CLIENTS);
    return ESP_OK;
}

esp_err_t mjpeg_stream_server_start(uint16_t port)
{
    /* Create TCP listen socket */
    s_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listen_sock < 0) {
        ESP_LOGE(TAG, "Failed to create listen socket: errno %d", errno);
        return ESP_FAIL;
    }

    int opt = 1;
    setsockopt(s_listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(s_listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "Failed to bind port %d: errno %d", port, errno);
        close(s_listen_sock);
        s_listen_sock = -1;
        return ESP_FAIL;
    }

    if (listen(s_listen_sock, LISTEN_BACKLOG) != 0) {
        ESP_LOGE(TAG, "Failed to listen on port %d: errno %d", port, errno);
        close(s_listen_sock);
        s_listen_sock = -1;
        return ESP_FAIL;
    }

    s_running = true;

    /* Spawn listen task on Core 1 */
    BaseType_t created = xTaskCreatePinnedToCore(
        mjpeg_listen_task,
        "mjpeg_listen",
        CLIENT_TASK_STACK,
        NULL,
        3,      /* slightly higher than client tasks */
        &s_listen_task,
        1);     /* Core 1 */

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create listen task");
        close(s_listen_sock);
        s_listen_sock = -1;
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "MJPEG streamer started on port %d", port);
    return ESP_OK;
}

int mjpeg_streamer_get_client_count(void)
{
    return get_client_count();
}

void mjpeg_streamer_stop(void)
{
    /* Stop the listen task */
    s_running = false;

    /* Close listen socket to unblock accept() */
    if (s_listen_sock >= 0) {
        close(s_listen_sock);
        s_listen_sock = -1;
    }

    /* 轮询等待任务自退（退出前自清 s_listen_task），最多 1s；仅当任务
     * 真正卡死（句柄仍非 NULL = TCB 仍有效）才强制删除——盲删可能已
     * 自退任务的悬垂句柄是 LoadProhibited 崩溃源（2026-09-07）。 */
    for (int i = 0; i < 100 && s_listen_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_listen_task != NULL) {
        vTaskDelete(s_listen_task);
        s_listen_task = NULL;
    }
    
    s_client_count = 0;
    
    if (s_mutex != NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "MJPEG streamer stopped, clients reset");
}