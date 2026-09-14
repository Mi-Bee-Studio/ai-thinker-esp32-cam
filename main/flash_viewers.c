/**
 * @file flash_viewers.c
 * @brief Viewer-driven flash LED watcher (board-local extension).
 *
 * 决策每秒一次：
 *   观看者 = (MJPEG :81 客户端数 > 0) || 最近 3s 内有 /api/capture 拍照。
 *   观看者在场 → LED 常亮；最后一位观看者离开后 GRACE_MS 内保持常亮
 *   （吸收 NVR 式 15-20s 重连churn，避免频闪），超过即熄灭。
 *
 * 与其他 LED 驱动方（motion 自动闪光、timelapse、手动 /api/led）的仲裁：
 *   - 功能开启且观看者在场时，watcher 每拍把 LED 拉回亮（其他驱动方熄灭后
 *     最迟 1s 恢复；motion 闪光脉冲结束后同理）——此语义下手动关闭仅在
 *     无观看者时可靠，属预期行为并在 AGENTS.md 记录；
 *   - 功能开启但已判熄（grace 过期）或功能关闭时，watcher 不再写 LED，
 *     手动控制完全自由。
 */

#include "flash_viewers.h"
#include "flash_led.h"
#include "config_manager.h"
#include "mjpeg_streamer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "flash_viewers";

#define WATCHER_TICK_MS      1000
#define WATCHER_STACK_SIZE   3072   /* PIT-039 红线：常驻任务静态栈 */
#define CAPTURE_ACTIVE_MS    (3 * 1000 * 1000LL)  /* 拍照后视为在场的窗口 */
#define GRACE_MS             (20 * 1000 * 1000LL) /* 最后观看者离开的保持窗 */

static StackType_t s_stack[WATCHER_STACK_SIZE / sizeof(StackType_t)];
static StaticTask_t s_tcb;
static TaskHandle_t s_task;

static volatile int64_t s_capture_until_us = 0; /* esp_timer 时间基 */
static volatile uint8_t s_forced_on = 0;        /* 当前是否处于 watcher 强制亮 */

uint8_t flash_viewers_active(void)
{
    return s_forced_on;
}

void flash_viewers_notify_capture(void)
{
    s_capture_until_us = esp_timer_get_time() + CAPTURE_ACTIVE_MS;
}

static bool viewer_present(void)
{
    if (mjpeg_streamer_get_client_count() > 0) return true;
    return esp_timer_get_time() < s_capture_until_us;
}

static void flash_viewers_task(void *arg)
{
    (void)arg;
    bool last_viewer = false;
    int64_t last_seen_us = 0;
    bool forced = false; /* 本任务当前是否已把 LED 拉亮 */

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(WATCHER_TICK_MS));

        const cam_config_t *cfg = config_get();
        if (!cfg->flash_viewers) {
            if (forced) {
                /* 开关被关闭：交还 LED 控制权，避免把灯滞留在亮态 */
                flash_led_off();
                ESP_LOGI(TAG, "disabled — LED released");
            }
            forced = false;
            s_forced_on = 0;
            last_viewer = false;
            continue;
        }

        bool viewer = viewer_present();
        if (viewer) {
            last_seen_us = esp_timer_get_time();
        }

        /* 在场判定（含 grace）：在场→亮；离场未超 grace→仍亮；超时→熄 */
        bool want_on = viewer || (esp_timer_get_time() - last_seen_us) < GRACE_MS;

        if (want_on) {
            if (!flash_led_is_on()) {
                flash_led_on();  /* 幂等收敛：motion 脉冲/手动熄灭后拉回 */
                if (!forced) {
                    ESP_LOGI(TAG, "viewer present — LED on");
                }
            }
            forced = true;
            s_forced_on = 1;
        } else if (forced) {
            flash_led_off();
            forced = false;
            s_forced_on = 0;
            ESP_LOGI(TAG, "no viewers — LED off (grace expired)");
        }

        if (viewer != last_viewer) {
            last_viewer = viewer;
        }
    }
}

void flash_viewers_start(void)
{
    if (s_task) return;
    s_task = xTaskCreateStatic(flash_viewers_task, "flash_viewers",
                               sizeof(s_stack) / sizeof(s_stack[0]),
                               NULL, tskIDLE_PRIORITY + 2, s_stack, &s_tcb);
    if (s_task == NULL) {
        ESP_LOGE(TAG, "watcher task create failed");
        return;
    }
    ESP_LOGI(TAG, "watcher started (1Hz, grace %ds, config default off)",
             (int)(GRACE_MS / 1000000LL));
}
