/*
 * SD Card Storage Manager for AI_Thinker ESP32-CAM
 *
 * SPI mode (SDSPI) via SPI2_HOST:
 *   CLK=GPIO14 (shared with camera XCLK — time-multiplexed!)
 *   MOSI=GPIO15, MISO=GPIO2, CS=GPIO13
 *
 * CRITICAL: GPIO14 is shared between SD card CLK and camera XCLK.
 * storage_deinit() must be called before camera init, and storage_init()
 * must be called before any SD card access. Never access both simultaneously.
 */

#include "storage_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "ff.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "storage";

#define MOUNT_POINT          "/sdcard"
#define PHOTOS_BASE_PATH     "/sdcard/photos"
#define HOTPLUG_POLL_SEC     10
#define CLEANUP_LOW_PCT      20.0f
#define CLEANUP_HIGH_PCT     30.0f
#define MAX_RETRIES          5

/* SPI host for SD card — SPI2_HOST (SPI3 may conflict with WiFi on ESP32) */
#define SD_SPI_HOST          SPI2_HOST

static bool s_mounted = false;
static SemaphoreHandle_t s_mutex = NULL;
static sdmmc_card_t *s_card = NULL;
static TaskHandle_t s_hotplug_task = NULL;

/* ---- Forward declarations ---- */
static void hotplug_monitor_task(void *arg);
static esp_err_t sdspi_mount_internal(bool format_if_failed);
static void sdspi_unmount_internal(void);
static uint32_t count_photos_recursive(const char *dirpath);

/* ---- Internal helpers ---- */

static esp_err_t sdspi_mount_internal(bool format_if_failed)
{
    /* SPI bus config */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_CLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4096,
    };
    esp_err_t ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* SDMMC host config */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    /* SPI device (slot) config — IDF v6.0 style */
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = SD_SPI_HOST;

    /* VFS FAT mount config */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_failed,
        .max_files = 5,
        .allocation_unit_size = 64 * 1024,
    };

    /* Retry SD init up to MAX_RETRIES times — some TF cards need extra time */
    const int delays_ms[] = {300, 500, 1000, 1000, 1000};
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        ESP_LOGI(TAG, "SD mount attempt %d/%d", attempt, MAX_RETRIES);
        ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
        if (ret == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "SD mount attempt %d failed: %s (0x%x)", attempt, esp_err_to_name(ret), (unsigned)ret);
        if (attempt < MAX_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(delays_ms[attempt - 1]));
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        spi_bus_free(SD_SPI_HOST);
        return ret;
    }

    return ESP_OK;
}

static void sdspi_unmount_internal(void)
{
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    spi_bus_free(SD_SPI_HOST);
    s_card = NULL;
    s_mounted = false;
}

/**
 * @brief Create YYYY-MM directory under PHOTOS_BASE_PATH using current time
 * @param dirpath  Output buffer for full directory path (min 64 bytes)
 * @return ESP_OK on success
 */
static esp_err_t ensure_photo_dir(char *dirpath, size_t dirpath_size)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(dirpath, dirpath_size, "%s/%04d-%02d", PHOTOS_BASE_PATH, tm.tm_year + 1900, tm.tm_mon + 1);

    struct stat st;
    /* Create base photos dir if missing */
    if (stat(PHOTOS_BASE_PATH, &st) != 0) {
        if (mkdir(PHOTOS_BASE_PATH, 0775) != 0 && errno != EEXIST) {
            ESP_LOGE(TAG, "Failed to create %s: %s", PHOTOS_BASE_PATH, strerror(errno));
            return ESP_FAIL;
        }
    }
    /* Create month directory */
    if (stat(dirpath, &st) != 0) {
        if (mkdir(dirpath, 0775) != 0 && errno != EEXIST) {
            ESP_LOGE(TAG, "Failed to create %s: %s", dirpath, strerror(errno));
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

/**
 * @brief Get free space percentage using FatFs f_getfree
 */
static float get_free_percent(void)
{
    if (!s_mounted) return 0.0f;

    DWORD free_clusters = 0;
    FATFS *fs = NULL;
    FRESULT res = f_getfree("0:", &free_clusters, &fs);
    if (res != FR_OK || !fs) {
        ESP_LOGE(TAG, "f_getfree failed: %d", res);
        return 0.0f;
    }
    uint32_t total_clusters = (uint32_t)(fs->n_fatent - 2);
    if (total_clusters == 0) return 0.0f;
    return (float)free_clusters / (float)total_clusters * 100.0f;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
/**
 * @brief Find and delete the oldest file under PHOTOS_BASE_PATH (recursive)
 * @return ESP_OK if a file was deleted, ESP_ERR_NOT_FOUND if none
 */
static esp_err_t delete_oldest_photo(void)
{
    char oldest_path[512] = {0};
    time_t oldest_mtime = 0;
    bool found = false;

    DIR *dir = opendir(PHOTOS_BASE_PATH);
    if (!dir) return ESP_ERR_NOT_FOUND;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", PHOTOS_BASE_PATH, entry->d_name);

        /* Recurse into subdirectories */
        if (entry->d_type == DT_DIR) {
            DIR *subdir = opendir(fullpath);
            if (!subdir) continue;
            struct dirent *sub_entry;
            while ((sub_entry = readdir(subdir)) != NULL) {
                if (sub_entry->d_name[0] == '.') continue;
                char subpath[512];
                snprintf(subpath, sizeof(subpath), "%s/%s", fullpath, sub_entry->d_name);
                struct stat st;
                if (stat(subpath, &st) == 0 && S_ISREG(st.st_mode)) {
                    if (!found || st.st_mtime < oldest_mtime) {
                        strncpy(oldest_path, subpath, sizeof(oldest_path) - 1);
                        oldest_mtime = st.st_mtime;
                        found = true;
                    }
                }
            }
            closedir(subdir);
            continue;
        }

        /* Regular files directly in photos base */
        struct stat st;
        if (stat(fullpath, &st) == 0 && S_ISREG(st.st_mode)) {
            if (!found || st.st_mtime < oldest_mtime) {
                strncpy(oldest_path, fullpath, sizeof(oldest_path) - 1);
                oldest_mtime = st.st_mtime;
                found = true;
            }
        }
    }
    closedir(dir);

    if (!found) return ESP_ERR_NOT_FOUND;

    if (remove(oldest_path) == 0) {
        ESP_LOGI(TAG, "Deleted oldest: %s", oldest_path);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to delete %s: %s", oldest_path, strerror(errno));
    return ESP_FAIL;
}
#pragma GCC diagnostic pop

static uint32_t count_photos_recursive(const char *dirpath)
{
    uint32_t count = 0;
    DIR *dir = opendir(dirpath);
    if (!dir) return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);

        if (entry->d_type == DT_DIR) {
            count += count_photos_recursive(fullpath);
        } else if (entry->d_type == DT_REG) {
            count++;
        }
    }
    closedir(dir);
    return count;
}

/**
 * @brief Hot-plug monitor task — polls every 10s, detects card removal/insertion
 */
static void hotplug_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "Hot-plug monitor started (poll every %ds)", HOTPLUG_POLL_SEC);

    bool was_present = s_mounted;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(HOTPLUG_POLL_SEC * 1000));

        xSemaphoreTake(s_mutex, portMAX_DELAY);

        /* Check card presence via f_stat on the root */
        FILINFO fno;
        FRESULT res = f_stat("/sdcard", &fno);
        bool is_present = (res == FR_OK);

        if (was_present && !is_present) {
            ESP_LOGW(TAG, "SD card removed!");
            sdspi_unmount_internal();
        } else if (!was_present && is_present && !s_mounted) {
            ESP_LOGI(TAG, "SD card inserted, attempting auto-mount...");
            esp_err_t err = sdspi_mount_internal(false);
            if (err == ESP_OK) {
                s_mounted = true;
                ESP_LOGI(TAG, "SD card auto-mounted");
                /* Re-create photos base directory */
                struct stat st;
                if (stat(PHOTOS_BASE_PATH, &st) != 0) {
                    mkdir(PHOTOS_BASE_PATH, 0775);
                }
            } else {
                ESP_LOGW(TAG, "SD auto-mount failed: %s", esp_err_to_name(err));
            }
        }

        was_present = s_mounted;
        xSemaphoreGive(s_mutex);
    }
}

/* ---- Public API ---- */

esp_err_t storage_init(void)
{
    esp_err_t ret;

    /* Create recursive mutex for storage operations */
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Initializing SD card (SPI mode): CS=%d CLK=%d MOSI=%d MISO=%d",
             SD_PIN_CS, SD_PIN_CLK, SD_PIN_MOSI, SD_PIN_MISO);

    /* Reset SD GPIO pins to default state */
    gpio_reset_pin(SD_PIN_CS);
    gpio_reset_pin(SD_PIN_CLK);
    gpio_reset_pin(SD_PIN_MOSI);
    gpio_reset_pin(SD_PIN_MISO);

    /* Allow SD card power to stabilize */
    vTaskDelay(pdMS_TO_TICKS(200));

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    ret = sdspi_mount_internal(false);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return ret;
    }

    s_mounted = true;

    /* Print card info */
    ESP_LOGI(TAG, "SD card mounted OK");
    ESP_LOGI(TAG, "  Type: %s", s_card->is_mmc ? "MMC" : "SD");
    ESP_LOGI(TAG, "  Name: %s", s_card->cid.name);
    ESP_LOGI(TAG, "  Size: %lluMB",
             ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024));

    /* Create photos base directory */
    struct stat st;
    if (stat(PHOTOS_BASE_PATH, &st) != 0) {
        mkdir(PHOTOS_BASE_PATH, 0775);
        ESP_LOGI(TAG, "Created photos directory");
    }

    xSemaphoreGive(s_mutex);

    /* Start hot-plug monitor task */
    xTaskCreate(hotplug_monitor_task, "sd_hotplug", 2048, NULL, 5, &s_hotplug_task);

    return ESP_OK;
}

esp_err_t storage_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing SD card (releasing GPIO14 for camera)");

    /* Stop hot-plug monitor */
    if (s_hotplug_task) {
        vTaskDelete(s_hotplug_task);
        s_hotplug_task = NULL;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    sdspi_unmount_internal();
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "SD card unmounted, SPI bus released");
    return ESP_OK;
}

bool storage_is_available(void)
{
    return s_mounted;
}

esp_err_t storage_check(void)
{
    if (!s_mounted) return ESP_FAIL;

    struct stat st;
    if (stat(MOUNT_POINT, &st) == 0) {
        return ESP_OK;
    }
    s_mounted = false;
    return ESP_FAIL;
}

esp_err_t storage_save_photo(camera_fb_t *fb, const char *filename)
{
    if (!s_mounted || !fb || !fb->buf || !filename) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Ensure YYYY-MM directory exists */
    char dirpath[128];
    esp_err_t ret = ensure_photo_dir(dirpath, sizeof(dirpath));
    if (ret != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return ret;
    }

    /* Build full file path */
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, filename);

    /* Write JPEG data to file */
    FILE *f = fopen(filepath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s: %s", filepath, strerror(errno));
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    size_t written = fwrite(fb->buf, 1, fb->len, f);
    fclose(f);

    if (written != fb->len) {
        ESP_LOGE(TAG, "Write incomplete: %zu/%zu bytes", written, fb->len);
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved photo: %s (%zu bytes)", filepath, fb->len);
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

int storage_list_photos(const char *path, char *buf, size_t buf_size)
{
    if (!s_mounted || !path || !buf || buf_size == 0) {
        return -1;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Cannot open directory %s: %s", path, strerror(errno));
        xSemaphoreGive(s_mutex);
        return -1;
    }

    int count = 0;
    size_t offset = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        /* Only list regular files */
        if (entry->d_type != DT_REG) continue;

        int needed = snprintf(buf + offset, buf_size - offset, "%s/%s\n", path, entry->d_name);
        if (offset + needed >= buf_size) {
            /* Buffer full — stop listing */
            ESP_LOGW(TAG, "Photo list buffer full after %d entries", count);
            break;
        }
        offset += needed;
        count++;
    }

    closedir(dir);
    xSemaphoreGive(s_mutex);
    return count;
}

esp_err_t storage_cleanup(void)
{
    if (!s_mounted) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    float free_pct = get_free_percent();
    ESP_LOGI(TAG, "Storage free: %.1f%%", free_pct);

    if (free_pct >= CLEANUP_LOW_PCT) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Storage low (%.1f%%), starting cleanup", free_pct);
    xSemaphoreGive(s_mutex);

    int deleted = 0;
    while (true) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        float pct = get_free_percent();
        xSemaphoreGive(s_mutex);

        if (pct >= CLEANUP_HIGH_PCT) break;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        esp_err_t err = delete_oldest_photo();
        xSemaphoreGive(s_mutex);

        if (err != ESP_OK) break;
        deleted++;
        if (deleted > 200) break;  /* Safety limit */
    }

    float new_pct = get_free_percent();
    ESP_LOGI(TAG, "Cleanup done: deleted %d files, free now %.1f%%", deleted, new_pct);
    return ESP_OK;
}

uint32_t storage_get_free_space(void)
{
    if (!s_mounted) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    DWORD free_clusters = 0;
    FATFS *fs = NULL;
    FRESULT res = f_getfree("0:", &free_clusters, &fs);
    if (res != FR_OK || !fs) {
        xSemaphoreGive(s_mutex);
        return 0;
    }

    uint64_t cluster_size = (uint64_t)fs->ssize * fs->csize;
    uint64_t free_bytes = (uint64_t)free_clusters * cluster_size;

    xSemaphoreGive(s_mutex);
    return (uint32_t)(free_bytes / (1024 * 1024));
}

uint32_t storage_get_photo_count(void)
{
    if (!s_mounted) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t count = count_photos_recursive(PHOTOS_BASE_PATH);
    xSemaphoreGive(s_mutex);
    return count;
}
