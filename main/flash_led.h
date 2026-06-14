#ifndef FLASH_LED_H
#define FLASH_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "camera_driver.h"

/**
 * @brief Initialize flash LED LEDC PWM (GPIO4, Timer1/Channel1)
 * Idempotent — safe to call multiple times.
 */
void flash_led_init(void);

/**
 * @brief Turn flash LED on (PWM ~80% duty)
 */
void flash_led_on(void);

/**
 * @brief Turn flash LED off
 */
void flash_led_off(void);

/**
 * @brief Check if flash LED is currently on (manual or auto-triggered)
 * @return true if LED is on
 */
bool flash_led_is_on(void);

/**
 * @brief Toggle flash LED between on/off
 * @return true if LED is now on, false if now off
 */
bool flash_led_toggle(void);

/**
 * @brief Detect scene brightness from a JPEG frame buffer using JPEG size heuristic.
 * Dark scenes produce smaller JPEGs due to uniform compression.
 * @param fb  Captured frame buffer (must be JPEG format)
 * @return Brightness percentage (0-100), where 0 = pitch black, 100 = very bright
 */
uint8_t flash_brightness_detect(camera_fb_t *fb);

/**
 * @brief Check if scene is dark based on config flash_threshold
 * @param brightness_pct  Brightness from flash_brightness_detect()
 * @return true if scene is dark
 */
bool flash_is_dark(uint8_t brightness_pct);

#endif // FLASH_LED_H