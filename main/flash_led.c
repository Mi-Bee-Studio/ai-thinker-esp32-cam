#include "flash_led.h"
#include "config_manager.h"
#include "esp_log.h"
#include "driver/ledc.h"

static const char *TAG = "flash_led";

#define FLASH_GPIO             4
#define FLASH_LEDC_TIMER       LEDC_TIMER_1
#define FLASH_LEDC_CHANNEL     LEDC_CHANNEL_1
#define FLASH_LEDC_SPEED       LEDC_LOW_SPEED_MODE
#define FLASH_PWM_FREQ         2000
#define FLASH_PWM_RES          LEDC_TIMER_8_BIT
#define FLASH_PWM_DUTY         205

static bool s_initialized = false;
static bool s_manual_on = false;  /* tracks manual on/off state (not auto-flash) */

void flash_led_init(void)
{
    if (s_initialized) return;

    ledc_timer_config_t timer_conf = {
        .speed_mode      = FLASH_LEDC_SPEED,
        .duty_resolution = FLASH_PWM_RES,
        .timer_num       = FLASH_LEDC_TIMER,
        .freq_hz         = FLASH_PWM_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return;
    }

    ledc_channel_config_t ch_conf = {
        .gpio_num   = FLASH_GPIO,
        .speed_mode = FLASH_LEDC_SPEED,
        .channel    = FLASH_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = FLASH_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Flash LED initialized (GPIO%d, ~80%% duty)", FLASH_GPIO);
}

void flash_led_on(void)
{
    flash_led_init();
    ledc_set_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL, FLASH_PWM_DUTY);
    ledc_update_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL);
    s_manual_on = true;
}

void flash_led_off(void)
{
    ledc_set_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL, 0);
    ledc_update_duty(FLASH_LEDC_SPEED, FLASH_LEDC_CHANNEL);
    s_manual_on = false;
}

bool flash_led_is_on(void)
{
    return s_manual_on;
}

bool flash_led_toggle(void)
{
    if (s_manual_on) {
        flash_led_off();
        return false;
    } else {
        flash_led_on();
        return true;
    }
}

uint8_t flash_brightness_detect(camera_fb_t *fb)
{
    if (!fb || fb->len == 0) return 50;

    uint32_t jpeg_kb = (uint32_t)fb->len / 1024;

    if (jpeg_kb >= 22) {
        return 100;
    } else if (jpeg_kb <= 12) {
        return 0;
    } else {
        return (uint8_t)((jpeg_kb - 12) * 100 / 10);
    }
}

bool flash_is_dark(uint8_t brightness_pct)
{
    const cam_config_t *cfg = config_get();
    return (brightness_pct < cfg->flash_threshold);
}