#include "sc_storage.h"
#include "esp_log.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
#include "esp_spiffs.h"
#else
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "esp_ldo_regulator.h"
#endif

static const char *TAG = "sc_storage";

#if !CONFIG_IDF_TARGET_ESP32S3
#define SD_PWR_EN_PIN GPIO_NUM_46

/* ESP-Hosted SDIO uses __attribute__((constructor)) to call sdmmc_host_init()
 * before app_main. ESP32-P4 has SDMMC_LL_HOST_CTLR_NUMS == 1, so we CANNOT
 * call sdmmc_host_init() again. These no-op wrappers let esp_vfs_fat_sdmmc_mount
 * believe the host was "freshly" initialized while skipping the actual init. */
static esp_err_t sdmmc_slot0_noop_init(void) { return ESP_OK; }
static esp_err_t sdmmc_slot0_noop_deinit(void) { return ESP_OK; }
#endif

esp_err_t sc_storage_init(void)
{
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
    ESP_LOGI(TAG, "Initializing SPIFFS storage...");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/sdcard",
        .partition_label = "storage",
        .max_files = 8,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted at /sdcard. Size: total %d, used %d", (int)total, (int)used);
    }
    return ESP_OK;
#else
    ESP_LOGI(TAG, "Initializing SD card via SDMMC...");

    // Enable internal LDO to power VDD_IO_5 (which powers GPIO46)
    esp_ldo_channel_handle_t ldo_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = 4,
        .voltage_mv = 3300,
    };
    esp_err_t ldo_err = esp_ldo_acquire_channel(&ldo_cfg, &ldo_chan);
    if (ldo_err == ESP_OK) {
        ESP_LOGI(TAG, "LDO Channel 4 acquired and set to 3.3V");
    } else {
        ESP_LOGW(TAG, "Failed to acquire LDO Channel 4 (%s)", esp_err_to_name(ldo_err));
    }

    ESP_LOGI(TAG, "Enabling SD Card Power on GPIO %d", SD_PWR_EN_PIN);
    gpio_config_t pwr_en_conf = {
        .pin_bit_mask = (1ULL << SD_PWR_EN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&pwr_en_conf);
    gpio_set_level(SD_PWR_EN_PIN, 1);

    // Some 256GB SDXC cards take over 1s to fully power up
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Build host descriptor — override init/deinit with no-ops since
     * ESP-Hosted's constructor already initialized the SDMMC host controller. */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot    = SDMMC_HOST_SLOT_0;       /* SD card uses Slot 0 IOMUX pins */
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.init    = sdmmc_slot0_noop_init;   /* skip: controller already up */
    host.deinit  = sdmmc_slot0_noop_deinit;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width  = 4;
    slot_config.d4 = GPIO_NUM_NC;
    slot_config.d5 = GPIO_NUM_NC;
    slot_config.d6 = GPIO_NUM_NC;
    slot_config.d7 = GPIO_NUM_NC;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card = NULL;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize the card (%s).", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted at /sdcard");
    sdmmc_card_print_info(stdout, card);

    return ESP_OK;
#endif
}
