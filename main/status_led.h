#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "common.h"

esp_err_t led_init(void);
void led_set_status(led_status_t status);

#endif
