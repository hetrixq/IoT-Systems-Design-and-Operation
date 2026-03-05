/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "dht.h"
#include "my_display.h"

#define TAG "METEO"

// Настройки I2C (для дисплея SSD1306)
#define I2C_MASTER_SCL_IO   GPIO_NUM_22
#define I2C_MASTER_SDA_IO   GPIO_NUM_21
#define I2C_MASTER_FREQ_HZ  400000
#define I2C_MASTER_NUM      I2C_NUM_0

// I2C адрес дисплея
#define I2C_SSD1306_ADDRESS 0x3C

// GPIO датчика DHT22
#define DHT22_GPIO          GPIO_NUM_4

// Период опроса датчика (мс, DHT22 минимум ~2 с)
#define SENSOR_PERIOD_MS    2000

// Структура данных, передаваемых через очередь
typedef struct {
    float temperature; // температура, °C
    float humidity;    // влажность, %
    bool  valid;       // true – данные корректны
} sensor_data_t;

// Дескриптор очереди (глобальный, доступен обеим задачам)
static QueueHandle_t s_sensor_queue = NULL;

// ─────────────────────────────────────────────────────────
// Задача чтения датчика DHT22
// ─────────────────────────────────────────────────────────
static void sensor_task(void *pvParam)
{
    (void)pvParam;

    sensor_data_t data;

    while (1)
    {
        float temp = 0.0f, hum = 0.0f;
        if (dht_read_float_data(DHT_TYPE_AM2301, DHT22_GPIO, &hum, &temp) == ESP_OK)
        {
            ESP_LOGI(TAG, "T=%.1f C  RH=%.1f %%", temp, hum);
            data.temperature = temp;
            data.humidity    = hum;
            data.valid       = true;
        }
        else
        {
            ESP_LOGW(TAG, "DHT22 read error");
            data.valid = false;
        }

        // Отправляем в очередь (без ожидания — старое значение заменит новое)
        xQueueOverwrite(s_sensor_queue, &data);

        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}

// ─────────────────────────────────────────────────────────
// Задача обновления дисплея
// ─────────────────────────────────────────────────────────
static void display_task(void *pvParam)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)pvParam;

    uint8_t *buf = heap_caps_malloc(SCREEN_WIDTH * SCREEN_HEIGHT / 8,
                                    MALLOC_CAP_DMA);
    if (!buf)
    {
        ESP_LOGE(TAG, "Не удалось выделить буфер дисплея!");
        vTaskDelete(NULL);
        return;
    }

    sensor_data_t data = {.valid = false};
    char line[24];

    while (1)
    {
        // Ждём новых данных (не более 3 с)
        xQueueReceive(s_sensor_queue, &data, pdMS_TO_TICKS(3000));

        clear_screen(buf);

        // Заголовок
        draw_string(buf, "=== METEO ===", 5, 0);

        if (data.valid)
        {
            // Температура с единицами измерения
            snprintf(line, sizeof(line), "Temp: %.1f C", data.temperature);
            draw_string(buf, line, 0, 18);

            // Влажность с единицами измерения
            snprintf(line, sizeof(line), "Hum:  %.1f %%", data.humidity);
            draw_string(buf, line, 0, 36);
        }
        else
        {
            draw_string(buf, "  NO DATA", 10, 28);
        }

        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0,
                                                  SCREEN_WIDTH, SCREEN_HEIGHT, buf));
    }

    free(buf);
    vTaskDelete(NULL);
}

// ─────────────────────────────────────────────────────────
// app_main: инициализация периферии и запуск задач
// ─────────────────────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "Инициализация I2C");

    // Шина I2C (для дисплея SSD1306)
    i2c_config_t i2c_conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_MASTER_SDA_IO,
        .sda_pullup_en    = GPIO_PULLUP_DISABLE,
        .scl_io_num       = I2C_MASTER_SCL_IO,
        .scl_pullup_en    = GPIO_PULLUP_DISABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_bus_handle_t i2c_bus = i2c_bus_create(I2C_MASTER_NUM, &i2c_conf);
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "Не удалось создать шину I2C");
        return;
    }

    // Инициализация дисплея SSD1306
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr              = I2C_SSD1306_ADDRESS,
        .scl_speed_hz          = I2C_MASTER_FREQ_HZ,
        .control_phase_bytes   = 1,
        .dc_bit_offset         = 6,
        .lcd_cmd_bits          = 8,
        .lcd_param_bits        = 8,
        .on_color_trans_done   = NULL,
        .user_ctx              = NULL,
        .flags = {.dc_low_on_data = 0, .disable_control_phase = 0},
    };
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(
        i2c_bus_get_internal_bus_handle(i2c_bus), &io_cfg, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
    };
    esp_lcd_panel_ssd1306_config_t ssd1306_cfg = {.height = SCREEN_HEIGHT};
    panel_cfg.vendor_config = &ssd1306_cfg;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_cfg, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    set_display_rotation(io_handle, true);

    // Очередь на 1 элемент (всегда актуальные данные)
    s_sensor_queue = xQueueCreate(1, sizeof(sensor_data_t));

    // Запуск задач
    xTaskCreate(sensor_task,  "sensor",  4096, NULL,         5, NULL);
    xTaskCreate(display_task, "display", 4096, panel_handle, 4, NULL);

    // app_main завершается — задачи продолжают работу
}

