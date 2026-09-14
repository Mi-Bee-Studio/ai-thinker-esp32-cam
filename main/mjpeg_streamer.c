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
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
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
/* 3000（原 15000）：弱链上单次阻塞 send 每多挂 1s，满载 TX 队列就多
 * 窒息 1s（wifi:mem fail → 连 :80 的 httpd 都发不出体数据，PIT-051）。
 * 3s 足够覆盖正常 RTT 抖动，超时即断该客户端释放队列。 */
#define SEND_TIMEOUT_MS     3000
#define CLIENT_RECV_TIMEOUT 5

/* 拥塞感知限速（PIT-051）：帧发送耗时是链路健康的直接度量——发得慢就
 * 丢帧降速，把 :81 的 TX 需求压到链路可承受的水平，给 Web/API 让出空口；
 * 连续快帧自动解除。观看端代价只是帧率下降，画质无损。 */
#define FRAME_SLOW_US       (1000 * 1000)  /* 单帧 >1s → 轻度限速 */
#define FRAME_VSLOW_US      (3000 * 1000)  /* 单帧 >3s → 重度限速 */
#define CONGEST_PACE_MS     2000           /* 轻度：1 帧 / 2s */
#define CONGEST_PACE_MAX_MS 5000           /* 重度：1 帧 / 5s 封顶 */
#define FRAME_FAST_US       (300 * 1000)   /* 单帧 <300ms */
#define FAST_STREAK_TO_CLEAR 3             /* 连续 3 个快帧解除限速 */

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

/* ---- 持久 worker 任务（PIT-039）：不再每连接 xTaskCreate ----
 * 初代 ESP32 内部 RAM 贴地（MALLOC_CAP_INTERNAL 常态 ~27KB 且高度碎片化），
 * 4KB 栈的客户端任务创建 ~50% 失败（实测 9 连败）——流"起来了又没有起来"。
 * 改为一个静态分配（xTaskCreateStatic，.bss 栈/TCB）的常驻 worker，
 * listen 任务经长度 1 的队列把 fd 递给它。零运行期任务创建。 */
static StaticTask_t   s_worker_tcb;
static StackType_t    s_worker_stack[CLIENT_TASK_STACK / sizeof(StackType_t)];
static TaskHandle_t   s_worker_task = NULL;      /* 首次 start 创建，永不删除 */
static QueueHandle_t  s_fd_queue = NULL;         /* 长度 1：int fd；-1 = 毒丸 */
static StaticTask_t   s_listen_tcb;
static StackType_t    s_listen_stack[CLIENT_TASK_STACK / sizeof(StackType_t)];

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

/* ---------- Client worker — persistent task, serves one fd per queue pickup ---------- */

static void mjpeg_client_task(void *arg)
{
    (void)arg;
    /* 常驻 worker：从队列取 fd → 服务到断开 → 回队列等待。任务与栈为
     * 静态分配，永不删除（stop 只投毒丸/踢 fd，worker 清理后回队列）。 */
    for (;;) {
        int client_sock = -1;
        if (xQueueReceive(s_fd_queue, &client_sock, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (client_sock < 0) {
            continue;   /* 毒丸（stop 时唤醒用）：无 fd 可服务，回队列等待 */
        }

        /* 占槽计数（listen 侧不再改计数；所有退出路径配对 --） */
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_client_count++;
        xSemaphoreGive(s_mutex);

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
        continue;
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
        continue;
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
        continue;
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

    /* 拥塞限速状态（PIT-051） */
    int congest_pace_ms = 0;          /* 0 = 不限速 */
    int fast_streak = 0;
    int64_t last_frame_done_us = 0;

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

        /* 限速窗内不取帧不发送——空口让给 :80/Web；100ms 粒度轮询到期 */
        if (congest_pace_ms > 0 &&
            (esp_timer_get_time() - last_frame_done_us) < (int64_t)congest_pace_ms * 1000) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
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

        int64_t frame_t0_us = esp_timer_get_time();

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

        /* 帧耗时分级：慢→限速丢帧，连续快→自动解除 */
        int64_t frame_ms = (esp_timer_get_time() - frame_t0_us) / 1000;
        last_frame_done_us = esp_timer_get_time();
        int prev_pace = congest_pace_ms;
        if (frame_ms * 1000 > FRAME_VSLOW_US) {
            congest_pace_ms = CONGEST_PACE_MAX_MS;
            fast_streak = 0;
        } else if (frame_ms * 1000 > FRAME_SLOW_US) {
            if (congest_pace_ms < CONGEST_PACE_MS) congest_pace_ms = CONGEST_PACE_MS;
            fast_streak = 0;
        } else if (frame_ms * 1000 < FRAME_FAST_US) {
            if (++fast_streak >= FAST_STREAK_TO_CLEAR) {
                congest_pace_ms = 0;
                fast_streak = 0;
            }
        } else {
            fast_streak = 0;
        }
        if (congest_pace_ms != prev_pace) {
            ESP_LOGW(TAG, "TX congestion pace -> %d ms/frame (last frame %lld ms)",
                     congest_pace_ms, (long long)frame_ms);
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
    }   /* for(;;) — 回队列等下一个连接 */
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

        /* 发送超时兜底（2026-09-04 家族同步，2026-09-13 收紧随 SEND_TIMEOUT_MS）：
         * TCP 零窗口客户端的 send() 会阻塞占住任务；超时让其走断开清理。 */
        struct timeval snd_to = { .tv_sec = SEND_TIMEOUT_MS / 1000, .tv_usec = 0 };
        setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, &snd_to, sizeof(snd_to));

        /* 对端溯源（PIT-038）：重连风暴/锤击定位，accept 即记 IP */
        {
            char cip[INET_ADDRSTRLEN] = "?";
            inet_ntop(AF_INET, &client_addr.sin_addr, cip, sizeof(cip));
            ESP_LOGI(TAG, "Stream accept from %s (internal=%u)",
                     cip,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        }

        /* 防锤击护栏（PIT-038 v2；PIT-039 修锁死）：同 IP 两次接入间隔 <5s
         * 才算"新违规"。退避窗口内守规矩的重连（≥5s 间隔，如 NVR 的 15s
         * 梯子）会被拒但**不续期**——窗口自然过期后即可重新入内。旧逻辑
         * 窗口内任何再撞都续期+翻倍封顶 300s，重连间隔短于 300s 的合法
         * 客户端（NVR 15s）被永久锁死（实测单日拒绝计数 20901 仍爬升）。
         * 真锤子（1-2s 风暴）每次再撞都是新违规 → 持续续期维持封禁。 */
        {
            enum { HAMMER_SLOTS = 4, HAMMER_MIN_GAP_MS = 5000 };
            static struct {
                struct in_addr peer;
                TickType_t last_seen;    /* 上次任意接入（放行或拒绝）时刻 */
                TickType_t until;        /* 退避截止 */
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
            bool in_window = (h >= 0 && (int32_t)(now - s_hammer[h].until) < 0);
            bool violation = (h >= 0 && (int32_t)(now - s_hammer[h].last_seen) <
                                            pdMS_TO_TICKS(HAMMER_MIN_GAP_MS));
            if (in_window || violation) {
                if (violation) {
                    /* 只有新违规才续期+翻倍；守规矩客户端等窗口自然过期 */
                    s_hammer[h].until = now + pdMS_TO_TICKS(s_hammer[h].backoff_ms);
                    s_hammer[h].backoff_ms = s_hammer[h].backoff_ms < 300000
                                                 ? s_hammer[h].backoff_ms * 2 : 300000;
                }
                s_hammer[h].last_seen = now;
                if (++s_hammer[h].rejected % 50 == 1) {
                    char ipstr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ipstr, sizeof(ipstr));
                    ESP_LOGI(TAG, "Hammer guard: rejected %u from %s (backoff %us)",
                             (unsigned)s_hammer[h].rejected, ipstr,
                             s_hammer[h].backoff_ms / 1000);
                }
                /* 503 带 Retry-After（剩余冷却秒数）：客户端遵守即可等过
                 * 窗口自然重新入内（2026-09-08 晚 MiBeeNvr#711 对账）。 */
                uint32_t retry_s = 1;
                if ((int32_t)(s_hammer[h].until - now) > 0) {
                    retry_s = ((uint32_t)(s_hammer[h].until - now)) /
                              pdMS_TO_TICKS(1000) + 1;
                }
                char busy[128];
                int bl = snprintf(busy, sizeof(busy),
                    "HTTP/1.1 503 Service Unavailable\r\n"
                    "Retry-After: %u\r\n"
                    "Content-Length: 22\r\n\r\nRetry after cooldown\r\n",
                    (unsigned)retry_s);
                if (bl > 0) {
                    send(client_sock, busy, bl, 0);
                }
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
            s_hammer[h].last_seen = now;
            s_hammer[h].backoff_ms = 10000;
        }

        /* 单槽位：已有连接（含滞留僵尸）一律让位给新连接（用户刚打开的页面）。
         * 踢除 = shutdown 旧 fd，worker 的 send/recv 立刻报错走清理；等
         * 注册表清空后把新 fd 递进队列（PIT-039：不再每连接建任务）。 */
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

        /* 计数由 worker 在登记时维护；这里只递 fd（队列长度 1，槽位已空
         * 必然成功——100ms 超时兜底防极端竞态） */
        if (xQueueSend(s_fd_queue, &client_sock, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "fd queue full — dropping connection");
            close(client_sock);
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

    /* fd 队列 + 常驻 worker（静态栈，首次创建后跨 stop/start 复用） */
    if (s_fd_queue == NULL) {
        s_fd_queue = xQueueCreate(1, sizeof(int));
    }
    if (s_fd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create fd queue");
        close(s_listen_sock);
        s_listen_sock = -1;
        s_running = false;
        return ESP_FAIL;
    }
    if (s_worker_task == NULL) {
        s_worker_task = xTaskCreateStaticPinnedToCore(
            mjpeg_client_task, "mjpeg_cli",
            CLIENT_TASK_STACK / sizeof(StackType_t), NULL,
            2, s_worker_stack, &s_worker_tcb, 1);
        if (s_worker_task == NULL) {
            ESP_LOGE(TAG, "Failed to create client worker task");
            close(s_listen_sock);
            s_listen_sock = -1;
            s_running = false;
            return ESP_FAIL;
        }
    }

    /* Listen task（静态栈——本板运行期堆碎片化下动态创建不可靠，PIT-039） */
    s_listen_task = xTaskCreateStaticPinnedToCore(
        mjpeg_listen_task, "mjpeg_listen",
        CLIENT_TASK_STACK / sizeof(StackType_t), NULL,
        3,      /* slightly higher than client worker */
        s_listen_stack, &s_listen_tcb, 1);     /* Core 1 */

    if (s_listen_task == NULL) {
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
     * 自退任务的悬垂句柄是 LoadProhibited 崩溃源（2026-09-07）。
     * 静态任务删除安全：IDLE 清理跳过静态内存，.bss 下次 start 复用。 */
    for (int i = 0; i < 100 && s_listen_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_listen_task != NULL) {
        vTaskDelete(s_listen_task);
        s_listen_task = NULL;
    }

    /* 常驻 worker 不删除：踢掉在服务连接，投毒丸清空队列，等它回空闲。
     * （最长阻塞点 = SO_SNDTIMEO 15s；等不到也无碍——worker 不持有
     * 相机/注册表之外的任何共享状态，清理路径自会走完。） */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_clients[0].fd != 0) {
        shutdown(s_clients[0].fd, SHUT_RDWR);
    }
    s_client_count = 0;
    xSemaphoreGive(s_mutex);

    int poison = -1;
    if (s_fd_queue != NULL) {
        xQueueSend(s_fd_queue, &poison, 0);
        int stale;
        while (xQueueReceive(s_fd_queue, &stale, 0) == pdTRUE && stale >= 0) {
            close(stale);   /* 排掉 stop 前夜挤进队列的 fd */
        }
    }

    /* 互斥锁与队列**不删**（worker 常驻复用）：旧实现 stop 删锁时若有
     * 客户端还在清理路径上，xSemaphoreTake 已删句柄 = 崩溃窗口。 */

    ESP_LOGI(TAG, "MJPEG streamer stopped, clients reset");
}