#ifndef TIMELAPSE_H
#define TIMELAPSE_H

#include "common.h"
#include "esp_err.h"
#include <stdint.h>

esp_err_t timelapse_init(void);
esp_err_t timelapse_start(void);
esp_err_t timelapse_stop(void);
bool timelapse_is_running(void);
uint32_t timelapse_get_photo_count(void);
uint32_t timelapse_get_burst_photo_count(void);
uint16_t timelapse_get_current_interval_s(void);
uint8_t timelapse_get_mode(void);

#endif