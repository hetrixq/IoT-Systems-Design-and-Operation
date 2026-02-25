/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты

DS18B20 1-Wire Temperature Sensor Example (GPIO bit-bang)
Uses esp-idf-lib/ds18x20 for Wokwi compatibility
*/
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <ds18x20.h>

static const char *TAG = "example";

#define ONEWIRE_GPIO        4
#define MAX_SENSORS         8
#define RESCAN_INTERVAL     5

void app_main(void)
{
    onewire_addr_t addrs[MAX_SENSORS];
    float temps[MAX_SENSORS];
    size_t sensor_count = 0;

    // Scan for DS18x20 devices on the bus
    ESP_LOGI(TAG, "Scanning 1-Wire bus on GPIO%d...", ONEWIRE_GPIO);
    esp_err_t err = ds18x20_scan_devices((gpio_num_t)ONEWIRE_GPIO, addrs, MAX_SENSORS, &sensor_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scanning failed: %s", esp_err_to_name(err));
        return;
    }
    if (sensor_count == 0) {
        ESP_LOGW(TAG, "No DS18x20 sensors found!");
        return;
    }

    ESP_LOGI(TAG, "Found %zu sensor(s)", sensor_count);
    for (size_t i = 0; i < sensor_count; i++) {
        ESP_LOGI(TAG, "  Sensor[%zu] address: %016llX", i, (unsigned long long)addrs[i]);
    }

    // Read temperatures in a loop
    while (1) {
        err = ds18x20_measure_and_read_multi((gpio_num_t)ONEWIRE_GPIO, addrs, sensor_count, temps);
        if (err == ESP_OK) {
            for (size_t i = 0; i < sensor_count; i++) {
                ESP_LOGI(TAG, "Sensor[%zu]: %.2f °C", i, temps[i]);
            }
        } else {
            ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
