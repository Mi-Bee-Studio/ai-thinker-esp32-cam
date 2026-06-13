#ifndef WEBHOOK_H
#define WEBHOOK_H

#include "esp_err.h"

esp_err_t webhook_init(void);
esp_err_t webhook_send_alert(const char *event_type, const char *message);

#endif // WEBHOOK_H
