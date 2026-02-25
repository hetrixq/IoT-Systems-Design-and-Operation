/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "i2cdev.h"
#include "bmp180.h"

#define I2C_MASTER_SCL_IO   GPIO_NUM_22
#define I2C_MASTER_SDA_IO   GPIO_NUM_21
#define I2C_MASTER_FREQ_HZ  100000

static const char *TAG = "bmp180_app";

void app_main(void)
{
    ESP_ERROR_CHECK(i2cdev_init());

    bmp180_dev_t bmp;
    memset(&bmp, 0, sizeof(bmp180_dev_t));

    ESP_ERROR_CHECK(bmp180_init_desc(&bmp, I2C_NUM_0, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO));

    bmp.i2c_dev.cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    bmp.i2c_dev.cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    bmp.i2c_dev.cfg.master.clk_speed = I2C_MASTER_FREQ_HZ;

    vTaskDelay(pdMS_TO_TICKS(500));

    esp_err_t err = bmp180_init(&bmp);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "BMP180 init failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "BMP180 initialized successfully");

    while (1)
    {
        float temperature = 0.0f;
        uint32_t pressure_pa = 0;

        err = bmp180_measure(&bmp, &temperature, &pressure_pa, BMP180_MODE_STANDARD);
        if (err == ESP_OK)
        {
            printf("Temperature: %.2f °C    Pressure: %lu Pa (%.2f hPa)\n",
                   temperature, (unsigned long)pressure_pa, pressure_pa / 100.0f);
        }
        else
        {
            ESP_LOGE(TAG, "bmp180_measure() error: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
