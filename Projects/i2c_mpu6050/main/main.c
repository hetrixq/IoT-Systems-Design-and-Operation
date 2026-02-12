#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "i2cdev.h"
#include "mpu6050.h"

#define I2C_MASTER_SCL_IO   GPIO_NUM_22
#define I2C_MASTER_SDA_IO   GPIO_NUM_21

static const char *TAG = "mpu6050_app";

void mpu6050_task(void *pvParameters)
{
    mpu6050_dev_t dev = { 0 };

    ESP_ERROR_CHECK(mpu6050_init_desc(&dev, MPU6050_I2C_ADDRESS_LOW, I2C_NUM_0, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO));

    // Wait for sensor to be reachable on the bus
    while (1)
    {
        esp_err_t res = i2c_dev_probe(&dev.i2c_dev, I2C_DEV_WRITE);
        if (res == ESP_OK)
        {
            ESP_LOGI(TAG, "MPU6050 found on I2C bus");
            break;
        }
        ESP_LOGE(TAG, "MPU6050 not found, retrying...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_ERROR_CHECK(mpu6050_init(&dev));
    ESP_LOGI(TAG, "MPU6050 initialized successfully");
    ESP_LOGI(TAG, "Accel range: %d, Gyro range: %d", dev.ranges.accel, dev.ranges.gyro);

    while (1)
    {
        float temperature = 0.0f;
        mpu6050_acceleration_t accel = { 0 };
        mpu6050_rotation_t rotation = { 0 };

        esp_err_t err = mpu6050_get_temperature(&dev, &temperature);
        if (err == ESP_OK)
            err = mpu6050_get_motion(&dev, &accel, &rotation);

        if (err == ESP_OK)
        {
            printf("Accel: x=%.4f g  y=%.4f g  z=%.4f g  |  "
                   "Gyro: x=%.4f °/s  y=%.4f °/s  z=%.4f °/s  |  "
                   "Temp: %.1f °C\n",
                   accel.x, accel.y, accel.z,
                   rotation.x, rotation.y, rotation.z,
                   temperature);
        }
        else
        {
            ESP_LOGE(TAG, "Read error: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(i2cdev_init());

    xTaskCreate(mpu6050_task, "mpu6050_task", configMINIMAL_STACK_SIZE * 6, NULL, 5, NULL);
}
