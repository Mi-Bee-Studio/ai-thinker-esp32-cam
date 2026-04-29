#ifndef HEALTH_MONITOR_H
#define HEALTH_MONITOR_H

#include "common.h"
#include "esp_err.h"

/**
 * @brief System health metrics snapshot
 */
typedef struct {
    uint32_t free_heap;           // Free heap in bytes
    uint32_t min_free_heap;       // Minimum free heap ever seen
    uint32_t total_heap;          // Total heap size
    uint32_t uptime_seconds;      // System uptime in seconds
    int8_t wifi_rssi;             // WiFi signal strength (dBm), -1 if not connected
    wifi_state_t wifi_state;      // Current WiFi state
    uint8_t cpu_usage_pct;        // Estimated CPU usage (0-100%)
    uint32_t photo_count;         // Total photos on SD card
    uint32_t sd_free_mb;          // SD card free space in MB
    bool camera_initialized;      // Camera status
    bool sd_mounted;              // SD card status
    uint32_t stream_clients;      // Active MJPEG stream clients
    uint32_t motion_events;       // Total motion events detected
    uint32_t nas_pending;         // NAS upload queue pending
} health_metrics_t;

/**
 * @brief Initialize health monitor and start periodic collection (30s interval)
 */
esp_err_t health_monitor_init(void);

/**
 * @brief Get pointer to current health metrics (static struct, do not free)
 */
const health_metrics_t *health_monitor_get_metrics(void);

/**
 * @brief Get uptime as human-readable string "Xd Xh Xm Xs" (static buffer)
 */
const char *health_monitor_get_uptime_str(void);

/**
 * @brief Get Prometheus-format metrics string (static buffer)
 */
const char *health_monitor_get_prometheus_str(void);

#endif // HEALTH_MONITOR_H
