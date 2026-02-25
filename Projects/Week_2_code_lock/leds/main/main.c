#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// пины подключения светодиодов
#define     LED_RED     GPIO_NUM_12
#define     LED_YELLOW  GPIO_NUM_14
#define     LED_GREEN   GPIO_NUM_27
#define     GPIO_PINS   ((1ULL << LED_RED) | (1ULL << LED_YELLOW) | (1ULL << LED_GREEN))

void app_main(void)
{
    // объявление структуры конфигурации
    gpio_config_t io_conf = {};

    // задание необходимых свойств
    io_conf.pin_bit_mask = GPIO_PINS;             // линии
    io_conf.mode = GPIO_MODE_OUTPUT;              // режим работы - выход
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;     // нет подтягивающего резистора
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; // нет стягивающего резистора
    io_conf.intr_type = GPIO_INTR_DISABLE;        // прерывания отключены

    // установка конфигурации GPIO
    gpio_config(&io_conf);

    // счетчик итераций
    uint8_t ticks = 0;

    while (1)
    {
        switch(++ticks % 8)
        {
            case 0:
                gpio_set_level(LED_RED, 0);
                gpio_set_level(LED_YELLOW, 0);
                gpio_set_level(LED_GREEN, 0);
                break;
            case 1:
                gpio_set_level(LED_RED, 1);
                break;
            case 2:
                gpio_set_level(LED_RED, 0);
                gpio_set_level(LED_YELLOW, 1);
                break;
            case 3:
                gpio_set_level(LED_YELLOW, 0);
                break;
            case 4:
                gpio_set_level(LED_GREEN, 1);
                break;
            case 5:
                gpio_set_level(LED_GREEN, 0);
                break;
            case 6:
                break;
            case 7:
                break;
        }
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}