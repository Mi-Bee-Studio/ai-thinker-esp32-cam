#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @file sd_logger.h
 * @brief Buffered SD card logger for severe errors and WiFi anomalies.
 *
 * Design:
 *   - Callers queue log entries via sd_logf() (non-blocking, drops if full).
 *   - A writer task drains the queue and batch-writes to SD card using
 *     only safe operations (mkdir / fopen("a") / fprintf / fclose).
 *   - Toggleable at runtime via config (sd_log_enabled) and web UI.
 *   - Log file: /sdcard/logs/system.log (single append-only file).
 *
 * Usage:
 *   sd_logf(SD_LOG_ERROR, "wifi", "disconnect reason=%d", reason);
 *   sd_logf(SD_LOG_WARN,  "camera", "fb_get timeout, fail_count=%d", n);
 */

typedef enum {
    SD_LOG_ERROR = 0,
    SD_LOG_WARN  = 1,
} sd_log_level_t;

/** Initialize logger (starts writer task). Reads enabled flag from config. */
esp_err_t sd_logger_init(void);

/** Enable/disable logging at runtime (also updates config flag). */
void sd_logger_set_enabled(bool enabled);
bool sd_logger_is_enabled(void);

/**
 * @brief Queue a log entry (non-blocking).
 *
 * Format string limited to ~160 chars.  If queue is full the entry is
 * silently dropped (never blocks the caller).
 *
 * @param level Severity level.
 * @param tag   Short module tag (e.g. "wifi", "camera", "http").
 * @param fmt   printf-style format string.
 */
void sd_logf(sd_log_level_t level, const char *tag, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

#endif /* SD_LOGGER_H */
