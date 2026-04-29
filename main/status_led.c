#include "status_led.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "status_led";

static volatile led_status_t current_status = LED_STARTING;
static TaskHandle_t led_task_handle = NULL;

// LED is active LOW: LOW = ON, HIGH = OFF
#define LED_ON  0
#define LED_OFF 1

static void led_task(void *arg)
{
    while (1) {
        switch (current_status) {
            case LED_STARTING:
                // Fast blink: 100ms on, 100ms off
                gpio_set_level(LED_PIN_STATUS, LED_ON);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(LED_PIN_STATUS, LED_OFF);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case LED_WIFI_CONNECTING:
                // Medium blink: 250ms on, 250ms off
                gpio_set_level(LED_PIN_STATUS, LED_ON);
                vTaskDelay(pdMS_TO_TICKS(250));
                gpio_set_level(LED_PIN_STATUS, LED_OFF);
                vTaskDelay(pdMS_TO_TICKS(250));
                break;

            case LED_RUNNING:
                // Slow blink: 2s on, 2s off
                gpio_set_level(LED_PIN_STATUS, LED_ON);
                vTaskDelay(pdMS_TO_TICKS(2000));
                gpio_set_level(LED_PIN_STATUS, LED_OFF);
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;

            case LED_ERROR:
                // Fast double blink
                gpio_set_level(LED_PIN_STATUS, LED_ON);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(LED_PIN_STATUS, LED_OFF);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(LED_PIN_STATUS, LED_ON);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(LED_PIN_STATUS, LED_OFF);
                vTaskDelay(pdMS_TO_TICKS(600));
                break;

            case LED_AP_MODE:
                // Solid on
                gpio_set_level(LED_PIN_STATUS, LED_ON);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;

            default:
                gpio_set_level(LED_PIN_STATUS, LED_OFF);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
        }
    }
}

esp_err_t led_init(void)
{
    esp_err_t ret;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_PIN_STATUS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start with LED OFF (GPIO HIGH for active-low)
    gpio_set_level(LED_PIN_STATUS, LED_OFF);

    // Create LED blink task
    BaseType_t task_ret = xTaskCreate(led_task, "led_task", 2048, NULL, 5, &led_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Status LED initialized on GPIO %d (active LOW)", LED_PIN_STATUS);
    return ESP_OK;
}

void led_set_status(led_status_t status)
{
    current_status = status;
    ESP_LOGD(TAG, "LED status set to %d", status);
}
