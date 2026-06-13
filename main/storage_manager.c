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
#include "config_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
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
#define RECORDINGS_PATH      "/sdcard/recordings"
#define FILE_CACHE_SIZE      500
#define MAX_RETRIES          5

/* SPI host for SD card — SPI2_HOST (SPI3 may conflict with WiFi on ESP32) */
#define SD_SPI_HOST          SPI2_HOST

static bool s_mounted = false;
static SemaphoreHandle_t s_mutex = NULL;
static sdmmc_card_t *s_card = NULL;
static TaskHandle_t s_hotplug_task = NULL;
static uint32_t s_cached_total_mb = 0;  /* Cached SD total capacity, set on mount */

/* Photo list cache — avoids traversing slow SPI SD on every request */
static char *s_list_cache = NULL;
static size_t s_list_cache_len = 0;
static int64_t s_list_cache_time = 0;
#define LIST_CACHE_TTL_MS  300000 /* 5 minute cache TTL (was 30s — slow SPI SD)*/

/* Recording file cache — ring buffer for circular cleanup */
static file_info_t s_file_cache[FILE_CACHE_SIZE];
static int s_file_cache_count = 0;

/* ---- Forward declarations ---- */
static void hotplug_monitor_task(void *arg);
static esp_err_t sdspi_mount_internal(bool format_if_failed);
static void sdspi_unmount_internal(void);
static uint32_t count_photos_recursive(const char *dirpath);
static void cache_sd_total_mb(void);
static int compare_file_info(const void *a, const void *b);
static void list_files_recursive(const char *dirpath, file_info_t *files, int max_count, int *count);
static void storage_rebuild_cache(void);
static int cleanup_incomplete_avi_recursive(const char *dirpath, int depth);

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
    s_cached_total_mb = 0;
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

    /* Guard: if RTC time is before 2024 (pre-NTP epoch), use unknown dir
     * instead of creating a 1970-* directory. */
    int year = tm.tm_year + 1900;
    if (year < 2024) {
        snprintf(dirpath, dirpath_size, "%s/unknown", PHOTOS_BASE_PATH);
    } else {
        snprintf(dirpath, dirpath_size, "%s/%04d-%02d", PHOTOS_BASE_PATH, year, tm.tm_mon + 1);
    }

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

        /* Check card presence via FatFs f_getfree on drive 0 */
        DWORD free_cls = 0;
        FATFS *ff = NULL;
        FRESULT res = f_getfree("0:", &free_cls, &ff);
        bool is_present = (res == FR_OK && ff != NULL);

        if (was_present && !is_present) {
            ESP_LOGW(TAG, "SD card removed!");
            sdspi_unmount_internal();
        } else if (!was_present && is_present && !s_mounted) {
            ESP_LOGI(TAG, "SD card inserted, attempting auto-mount...");
            esp_err_t err = sdspi_mount_internal(false);
            if (err == ESP_OK) {
                s_mounted = true;
                cache_sd_total_mb();
                ESP_LOGI(TAG, "SD card auto-mounted (%luMB)", (unsigned long)s_cached_total_mb);
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

/* ---- Recording file helpers ---- */

static int compare_file_info(const void *a, const void *b)
{
    return strcmp(((const file_info_t *)a)->name, ((const file_info_t *)b)->name);
}

static void list_files_recursive(const char *dirpath, file_info_t *files, int max_count, int *count)
{
    DIR *dir = opendir(dirpath);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && *count < max_count) {
        if (entry->d_name[0] == '.') continue;

        if (entry->d_type == DT_DIR) {
            char fullpath[300];
            int needed = snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
            if (needed < 0 || needed >= (int)sizeof(fullpath)) continue;
            list_files_recursive(fullpath, files, max_count, count);
            continue;
        }

        /* Only process .avi files */
        size_t nlen = strlen(entry->d_name);
        if (nlen < 5 || strcmp(entry->d_name + nlen - 4, ".avi") != 0) continue;

        char fullpath[300];
        int needed = snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
        if (needed < 0 || needed >= (int)sizeof(fullpath)) continue;

        struct stat st;
        if (stat(fullpath, &st) != 0) continue;

        /* Store relative path from RECORDINGS_PATH */
        const char *relpath = fullpath + strlen(RECORDINGS_PATH) + 1;
        strncpy(files[*count].name, relpath, sizeof(files[*count].name) - 1);
        files[*count].name[sizeof(files[*count].name) - 1] = '\0';
        files[*count].size = (uint32_t)st.st_size;

        struct tm *tm_info = localtime(&st.st_mtime);
        strftime(files[*count].time_str, sizeof(files[*count].time_str),
                 "%Y-%m-%d %H:%M:%S", tm_info);

        (*count)++;
    }
    closedir(dir);
}

static void storage_rebuild_cache(void)
{
    if (!s_mounted) {
        ESP_LOGW(TAG, "SD not mounted, skipping cache rebuild");
        return;
    }

    file_info_t *temp_files = malloc(FILE_CACHE_SIZE * sizeof(file_info_t));
    if (!temp_files) {
        ESP_LOGE(TAG, "No memory for file cache rebuild");
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int count = 0;
    list_files_recursive(RECORDINGS_PATH, temp_files, FILE_CACHE_SIZE, &count);

    /* Sort by name ascending (oldest timestamp first) */
    qsort(temp_files, count, sizeof(file_info_t), compare_file_info);

    s_file_cache_count = (count < FILE_CACHE_SIZE) ? count : FILE_CACHE_SIZE;
    for (int i = 0; i < s_file_cache_count; i++) {
        s_file_cache[i] = temp_files[i];
    }
    xSemaphoreGive(s_mutex);

    free(temp_files);
    ESP_LOGI(TAG, "File cache rebuilt: %d recordings found", s_file_cache_count);
}

static int cleanup_incomplete_avi_recursive(const char *dirpath, int depth)
{
    if (depth > 3) return 0;

    DIR *dir = opendir(dirpath);
    if (!dir) return 0;

    int deleted = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char fullpath[300];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            deleted += cleanup_incomplete_avi_recursive(fullpath, depth + 1);
            continue;
        }

        /* Check if it's an .avi file */
        size_t nlen = strlen(entry->d_name);
        if (nlen < 5 || strcmp(entry->d_name + nlen - 4, ".avi") != 0) continue;

        bool should_delete = false;

        /* Empty file (0 bytes) */
        if (st.st_size == 0) {
            should_delete = true;
            ESP_LOGI(TAG, "Found empty AVI (0 bytes): %s", fullpath);
        } else if (st.st_size < 260) {  /* RIFF(12) + hdrl(236) + movi(12) = 260 */
            should_delete = true;
            ESP_LOGI(TAG, "Found undersized AVI (%ld bytes): %s", (long)st.st_size, fullpath);
        } else {
            /* Read first 12 bytes: RIFF(4) + size(4) + AVI(4) */
            FILE *fp = fopen(fullpath, "rb");
            if (!fp) continue;
            uint8_t hdr[12];
            size_t n = fread(hdr, 1, 12, fp);
            fclose(fp);

            if (n == 12 && memcmp(hdr, "RIFF", 4) == 0) {
                uint32_t riff_size = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                                     ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
                if (riff_size == 0) {
                    should_delete = true;
                    ESP_LOGI(TAG, "Found incomplete AVI (RIFF size=0): %s", fullpath);
                }
            }
        }

        if (should_delete) {
            if (remove(fullpath) == 0) {
                deleted++;
                ESP_LOGI(TAG, "Deleted incomplete: %s", fullpath);
            }
        }
    }
    closedir(dir);
    return deleted;
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

    /* Cache total SD capacity (never changes) */
    cache_sd_total_mb();

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

    /* Create recordings directory */
    if (stat(RECORDINGS_PATH, &st) != 0) {
        mkdir(RECORDINGS_PATH, 0775);
        ESP_LOGI(TAG, "Created recordings directory");
    }

    /* Boot cleanup: remove incomplete AVI files */
    {
        int incomplete_deleted = cleanup_incomplete_avi_recursive(RECORDINGS_PATH, 0);
        if (incomplete_deleted > 0) {
            ESP_LOGI(TAG, "Boot cleanup: removed %d incomplete recordings", incomplete_deleted);
        }
    }

    /* Rebuild file cache from existing recordings */
    storage_rebuild_cache();

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
    s_cached_total_mb = 0;
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

    if (s_list_cache) {
        free(s_list_cache);
        s_list_cache = NULL;
        s_list_cache_len = 0;
        s_list_cache_time = 0;
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

static int list_photos_recursive(const char *dirpath, const char *base, char *buf, size_t buf_size, size_t *offset)
{
    DIR *dir = opendir(dirpath);
    if (!dir) {
        ESP_LOGE(TAG, "Cannot open directory %s: %s", dirpath, strerror(errno));
        return 0;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);

        if (entry->d_type == DT_DIR) {
            /* Recurse into subdirectory (e.g. YYYY-MM) */
            const char *rel = (base[0] == '\0') ? entry->d_name : NULL;
            char subrel[512];
            if (rel) {
                snprintf(subrel, sizeof(subrel), "%s", entry->d_name);
            } else {
                snprintf(subrel, sizeof(subrel), "%s/%s", base, entry->d_name);
            }
            count += list_photos_recursive(fullpath, subrel, buf, buf_size, offset);
        } else if (entry->d_type == DT_REG) {
            /* Get file size */
            struct stat st;
            long fsize = 0;
            if (stat(fullpath, &st) == 0) fsize = st.st_size;

            const char *rel = (base[0] == '\0') ? entry->d_name : NULL;
            int needed;
            if (rel) {
                needed = snprintf(buf + *offset, buf_size - *offset, "%s\t%ld\n", entry->d_name, fsize);
            } else {
                needed = snprintf(buf + *offset, buf_size - *offset, "%s/%s\t%ld\n", base, entry->d_name, fsize);
            }
            if (*offset + needed >= buf_size) {
                ESP_LOGW(TAG, "Photo list buffer full after %d entries", count);
                break;
            }
            *offset += needed;
            count++;
        }
    }

    closedir(dir);
    return count;
}

int storage_list_photos(const char *path, char *buf, size_t buf_size)
{
    if (!s_mounted || !path || !buf || buf_size == 0) {
        return -1;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    int64_t now = esp_timer_get_time();
    int64_t cache_age_ms = (now - s_list_cache_time) / 1000;

    if (s_list_cache && s_list_cache_len > 0 && cache_age_ms < LIST_CACHE_TTL_MS) {
        size_t copy_len = s_list_cache_len < buf_size ? s_list_cache_len : buf_size - 1;
        memcpy(buf, s_list_cache, copy_len);
        buf[copy_len] = '\0';
        int count = 0;
        for (size_t i = 0; i < copy_len; i++) {
            if (buf[i] == '\n') count++;
        }
        ESP_LOGD(TAG, "Photo list served from cache (%d entries, age %lldms)", count, cache_age_ms);
        xSemaphoreGive(s_mutex);
        return count;
    }

    if (s_list_cache) {
        free(s_list_cache);
        s_list_cache = NULL;
    }

    size_t offset = 0;
    int count = list_photos_recursive(path, "", buf, buf_size, &offset);

    s_list_cache_len = offset;
    s_list_cache = malloc(s_list_cache_len + 1);
    if (s_list_cache) {
        memcpy(s_list_cache, buf, s_list_cache_len);
        s_list_cache[s_list_cache_len] = '\0';
        s_list_cache_time = now;
        ESP_LOGI(TAG, "Photo list cached: %d entries, %zu bytes", count, s_list_cache_len);
    }

    xSemaphoreGive(s_mutex);
    return count;
}

void storage_invalidate_list_cache(void)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_list_cache) {
        free(s_list_cache);
        s_list_cache = NULL;
        s_list_cache_len = 0;
        s_list_cache_time = 0;
        ESP_LOGD(TAG, "Photo list cache invalidated");
    }
    xSemaphoreGive(s_mutex);
}

esp_err_t storage_delete_photo(const char *name)
{
    if (!s_mounted || !name) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    char filepath[280];
    snprintf(filepath, sizeof(filepath), "/sdcard/photos/%s", name);

    if (remove(filepath) != 0 && rmdir(filepath) != 0) {
        ESP_LOGW(TAG, "Failed to delete %s: %s", filepath, strerror(errno));
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleted: %s", filepath);

    if (s_list_cache) {
        free(s_list_cache);
        s_list_cache = NULL;
        s_list_cache_len = 0;
        s_list_cache_time = 0;
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t storage_cleanup(void)
{
    if (!s_mounted) return ESP_ERR_INVALID_STATE;

    const cam_config_t *cfg = config_get();
    uint8_t usage = storage_get_usage_pct();

    ESP_LOGI(TAG, "Storage usage: %d%% (cleanup threshold: %d%%)", usage, cfg->cleanup_low_pct);

    if (usage <= cfg->cleanup_low_pct) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Storage usage %d%% exceeds %d%%, starting cleanup", usage, cfg->cleanup_low_pct);

    int deleted = 0;
    while (true) {
        usage = storage_get_usage_pct();
        if (usage < cfg->cleanup_high_pct) break;

        /* Find and delete oldest recording file from cache */
        xSemaphoreTake(s_mutex, portMAX_DELAY);

        char oldest_path[512] = {0};
        time_t oldest_mtime = 0;
        bool found = false;

        #pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        for (int i = 0; i < s_file_cache_count; i++) {
            char fullpath[512];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", RECORDINGS_PATH, s_file_cache[i].name);

            struct stat st;
            if (stat(fullpath, &st) == 0 && S_ISREG(st.st_mode)) {
                if (!found || st.st_mtime < oldest_mtime) {
                    strncpy(oldest_path, fullpath, sizeof(oldest_path) - 1);
                    oldest_path[sizeof(oldest_path) - 1] = '\0';
                    oldest_mtime = st.st_mtime;
                    found = true;
                }
            }
        }
#pragma GCC diagnostic pop

        if (!found) {
            xSemaphoreGive(s_mutex);
            ESP_LOGW(TAG, "No recording files to delete");
            break;
        }

        if (remove(oldest_path) == 0) {
            ESP_LOGI(TAG, "Deleted oldest recording: %s", oldest_path);
            deleted++;

            /* Remove from cache */
            const char *rel = oldest_path + strlen(RECORDINGS_PATH) + 1;
            for (int i = 0; i < s_file_cache_count; i++) {
                if (strcmp(s_file_cache[i].name, rel) == 0) {
                    for (int j = i; j < s_file_cache_count - 1; j++) {
                        s_file_cache[j] = s_file_cache[j + 1];
                    }
                    s_file_cache_count--;
                    break;
                }
            }
        } else {
            ESP_LOGE(TAG, "Failed to delete %s: %s", oldest_path, strerror(errno));
        }

        xSemaphoreGive(s_mutex);

        if (deleted > 200) break;  /* Safety limit */
    }

    uint8_t new_usage = storage_get_usage_pct();
    ESP_LOGI(TAG, "Cleanup done: deleted %d files, usage now %d%%", deleted, new_usage);
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

uint32_t storage_get_total_space(void)
{
    /* Total capacity never changes — return cached value */
    return s_cached_total_mb;
}

void storage_warm_cache(void)
{
    if (!s_mounted) return;

    char *buf = malloc(32768);
    if (!buf) {
        ESP_LOGW(TAG, "Cannot allocate buffer for cache warm-up");
        return;
    }

    ESP_LOGI(TAG, "Warming photo list cache...");
    int count = storage_list_photos(PHOTOS_BASE_PATH, buf, 32768);
    ESP_LOGI(TAG, "Photo list cache warmed: %d entries", count);
    free(buf);
}

esp_err_t storage_format(void)
{
    if (!s_mounted || !s_card) {
        ESP_LOGE(TAG, "SD card not mounted, cannot format");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    ESP_LOGW(TAG, "=== FORMATTING SD CARD - ALL DATA WILL BE LOST ===");

    /* Invalidate photo list cache */
    if (s_list_cache) {
        free(s_list_cache);
        s_list_cache = NULL;
        s_list_cache_len = 0;
        s_list_cache_time = 0;
    }

    /* Format the SD card via VFS FatFs (unmounts, formats, remounts) */
    esp_err_t ret = esp_vfs_fat_sdcard_format(MOUNT_POINT, s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card format failed: %s", esp_err_to_name(ret));
        s_mounted = false;
        xSemaphoreGive(s_mutex);
        return ret;
    }

    /* Recreate photos and recordings directories */
    mkdir(PHOTOS_BASE_PATH, 0775);
    mkdir(RECORDINGS_PATH, 0775);

    /* Reset recording file cache */
    s_file_cache_count = 0;

    /* Recalculate total space (may differ after format on some cards) */
    cache_sd_total_mb();

    /* Rebuild recording file cache */
    storage_rebuild_cache();
    ESP_LOGI(TAG, "SD card formatted successfully");
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

/* ---- Recording file public API ---- */

void storage_register_file(const char *filepath, uint32_t size)
{
    if (!s_mounted || !filepath) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    int idx;
    if (s_file_cache_count < FILE_CACHE_SIZE) {
        idx = s_file_cache_count;
        s_file_cache_count++;
    } else {
        /* Ring buffer full — shift all entries left, evict oldest */
        for (int i = 0; i < FILE_CACHE_SIZE - 1; i++) {
            s_file_cache[i] = s_file_cache[i + 1];
        }
        idx = FILE_CACHE_SIZE - 1;
    }

    /* Store relative path from /sdcard/recordings/ */
    const char *relpath = filepath;
    const char *prefix = RECORDINGS_PATH "/";
    if (strncmp(filepath, prefix, strlen(prefix)) == 0) {
        relpath = filepath + strlen(prefix);
    }
    strncpy(s_file_cache[idx].name, relpath, sizeof(s_file_cache[idx].name) - 1);
    s_file_cache[idx].name[sizeof(s_file_cache[idx].name) - 1] = '\0';
    s_file_cache[idx].size = size;
    s_file_cache[idx].time_str[0] = '\0';

    xSemaphoreGive(s_mutex);

    /* Check if cleanup is needed after registering a new file */
    uint8_t usage = storage_get_usage_pct();
    if (usage > config_get()->cleanup_low_pct) {
        ESP_LOGW(TAG, "Usage %d%% > %d%%, triggering cleanup",
                 usage, config_get()->cleanup_low_pct);
        storage_cleanup();
    }
}

uint8_t storage_get_usage_pct(void)
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

    uint32_t total_clusters = (uint32_t)(fs->n_fatent - 2);
    if (total_clusters == 0) {
        xSemaphoreGive(s_mutex);
        return 0;
    }

    uint32_t used_clusters = total_clusters - free_clusters;
    uint8_t pct = (uint8_t)((uint64_t)used_clusters * 100 / total_clusters);

    xSemaphoreGive(s_mutex);
    return pct;
}

esp_err_t storage_cleanup_incomplete_avi(void)
{
    if (!s_mounted) return ESP_ERR_INVALID_STATE;

    struct stat st;
    if (stat(RECORDINGS_PATH, &st) != 0) {
        ESP_LOGW(TAG, "Recordings directory does not exist");
        return ESP_ERR_NOT_FOUND;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int deleted = cleanup_incomplete_avi_recursive(RECORDINGS_PATH, 0);
    xSemaphoreGive(s_mutex);

    if (deleted > 0) {
        ESP_LOGI(TAG, "Incomplete AVI cleanup: removed %d files", deleted);
    } else {
        ESP_LOGD(TAG, "No incomplete AVI files found");
    }

    return ESP_OK;
}

uint32_t storage_get_recording_count(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t count = (uint32_t)s_file_cache_count;
    xSemaphoreGive(s_mutex);
    return count;
}
static void cache_sd_total_mb(void)
{
    DWORD free_clusters = 0;
    FATFS *fs = NULL;
    FRESULT res = f_getfree("0:", &free_clusters, &fs);
    if (res != FR_OK || !fs) {
        s_cached_total_mb = 0;
        return;
    }
    uint64_t total_sectors = (uint64_t)(fs->n_fatent - 2) * fs->csize;
    uint64_t sector_size = (uint64_t)fs->ssize;
    uint64_t total_bytes = total_sectors * sector_size;
    s_cached_total_mb = (uint32_t)(total_bytes / (1024 * 1024));
}
