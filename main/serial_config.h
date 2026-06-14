#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#include "esp_err.h"

/**
 * @brief Start serial AT command listener on UART0
 * @return ESP_OK on success
 */
esp_err_t serial_config_init(void);

#endif // SERIAL_CONFIG_H
