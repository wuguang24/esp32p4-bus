/**
 ******************************************************************************
 * @file        sdmmc.c
 * @brief       TF 卡：与 ESP-Hosted 共用 SDMMC 控制器（Slot0=卡 / Slot1=C6）
 ******************************************************************************
 */

#include "sdmmc.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

const char *sdmmc_tag = "sdmmc";
sdmmc_card_t *card = NULL;
const char mount_point[] = MOUNT_POINT;
uint8_t sdmmc_mount_flag = 0x00;

#if CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0))
#define WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT 1
#else
#define WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT 0
#endif

#if WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT
static esp_err_t sdmmc_host_init_dummy(void)
{
    ESP_LOGI(sdmmc_tag, "sdmmc_host already initialized by ESP-Hosted, skipping init");
    return ESP_OK;
}
#endif

static esp_err_t sdmmc_try_mount(int width, int max_freq_khz)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t sdmmc_host = SDMMC_HOST_DEFAULT();
    sdmmc_host.slot = SDMMC_HOST_SLOT_0;
    sdmmc_host.max_freq_khz = max_freq_khz;

#if WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT
    /* 只替换 init；必须保留 DEINIT_ARG + deinit_p=sdmmc_host_deinit_slot，
     * 否则挂载失败后 Slot0 残留，重试会报 slot is not available (0x103) */
    sdmmc_host.init = &sdmmc_host_init_dummy;
    if (width >= 4) {
        sdmmc_host.flags = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_DDR | SDMMC_HOST_FLAG_DEINIT_ARG;
    } else {
        sdmmc_host.flags = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_DEINIT_ARG;
    }
#endif

    sdmmc_slot_config_t sdmmc_config = SDMMC_SLOT_CONFIG_DEFAULT();
    sdmmc_config.width = width;
    sdmmc_config.clk = SDMMC_PIN_CLK;
    sdmmc_config.cmd = SDMMC_PIN_CMD;
    sdmmc_config.d0 = SDMMC_PIN_D0;
    sdmmc_config.d1 = SDMMC_PIN_D1;
    sdmmc_config.d2 = SDMMC_PIN_D2;
    sdmmc_config.d3 = SDMMC_PIN_D3;
    sdmmc_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &sdmmc_host, &sdmmc_config,
                                            &mount_config, &card);
    if (ret == ESP_OK) {
        ESP_LOGI(sdmmc_tag, "mounted %d-bit @ %d kHz", width, max_freq_khz);
    }
    return ret;
}

esp_err_t sdmmc_init(void)
{
    if (sdmmc_mount_flag == 0x01 && card != NULL) {
        (void)sdmmc_unmount();
    }

    sd_dev_bsp_enable_phy_power();
    /* 上电稳定后再探卡，减少 OCR timeout */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* 先默认 4bit，再 1bit；失败路径会 deinit_slot 释放 Slot0 */
    esp_err_t ret = sdmmc_try_mount(4, SDMMC_FREQ_DEFAULT);
    if (ret != ESP_OK) {
        ESP_LOGW(sdmmc_tag, "4bit fail (%s), try 1bit", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(50));
        ret = sdmmc_try_mount(1, SDMMC_FREQ_DEFAULT);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(sdmmc_tag, "SD mount failed (%s). Insert card / check pull-ups.",
                 esp_err_to_name(ret));
        card = NULL;
        sdmmc_mount_flag = 0xFF;
        return ESP_FAIL;
    }

    sdmmc_card_print_info(stdout, card);
    sdmmc_mount_flag = 0x01;
    return ESP_OK;
}

esp_err_t sdmmc_unmount(void)
{
    if (card == NULL || sdmmc_mount_flag != 0x01) {
        card = NULL;
        sdmmc_mount_flag = 0x00;
        return ESP_OK;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(mount_point, card);
    card = NULL;
    sdmmc_mount_flag = 0x00;
    if (err != ESP_OK) {
        ESP_LOGW(sdmmc_tag, "unmount: %s", esp_err_to_name(err));
    }
    return err;
}

void sd_dev_bsp_enable_phy_power(void)
{
    static bool s_pwr_gpio;
    if (!s_pwr_gpio) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << SD_PWR_EN_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&io) == ESP_OK) {
            gpio_set_level(SD_PWR_EN_GPIO, 1);
            s_pwr_gpio = true;
        }
    }
#ifdef SD_PHY_PWR_LDO_CHAN
    static bool s_phy_on;
    if (s_phy_on) {
        return;
    }
    esp_ldo_channel_handle_t ldo_sd_phy = NULL;
    esp_ldo_channel_config_t ldo_sd_phy_config = {
        .chan_id = SD_PHY_PWR_LDO_CHAN,
        .voltage_mv = SD_PHY_PWR_LDO_VOLTAGE_MV,
    };
    esp_err_t err = esp_ldo_acquire_channel(&ldo_sd_phy_config, &ldo_sd_phy);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_phy_on = true;
        ESP_LOGI(sdmmc_tag, "SD PHY Powered on");
    } else {
        ESP_LOGW(sdmmc_tag, "SD PHY LDO: %s", esp_err_to_name(err));
    }
#endif
}
