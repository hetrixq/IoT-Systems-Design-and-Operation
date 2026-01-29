#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"

// Макрос для перевода номера GPIO в битовую маску
#define GPIO_BIT(gpio_num) (1ULL << (gpio_num))

// LED
#define LED GPIO_NUM_15

#define BTN1 27
#define BTN2 14
#define BTN3 26
#define BTN4 13

static inline bool is_pin_set(uint64_t data, uint64_t pin_mask)
{
    return (data & pin_mask) != 0;
}

void app_main(void)
{
    // Кнопки: вход + подтяжка вверх (кнопка замыкает на GND => при нажатии будет 0)
    gpio_config_t io_config = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = GPIO_BIT(BTN1) | GPIO_BIT(BTN2) | GPIO_BIT(BTN3) | GPIO_BIT(BTN4),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_config);

    // Светодиод
    gpio_set_direction(LED, GPIO_MODE_OUTPUT);

    bool led_state = false;
    uint32_t delay_ms = 1000;

    while (1)
    {
        uint64_t io_data = REG_READ(GPIO_IN_REG);

        // Для наглядности: 1 = отпущено, 0 = нажато (из-за pull-up)
        printf("BTN27=%d BTN14=%d BTN26=%d BTN13=%d\r\n",
               is_pin_set(io_data, GPIO_BIT(BTN1)),
               is_pin_set(io_data, GPIO_BIT(BTN2)),
               is_pin_set(io_data, GPIO_BIT(BTN3)),
               is_pin_set(io_data, GPIO_BIT(BTN4)));

        // Нажатая кнопка => пин = 0 (LOW)
        if (!is_pin_set(io_data, GPIO_BIT(BTN2))) delay_ms = 1000; // GPIO14
        if (!is_pin_set(io_data, GPIO_BIT(BTN4))) delay_ms = 500;  // GPIO13
        if (!is_pin_set(io_data, GPIO_BIT(BTN3))) delay_ms = 250;  // GPIO26
        if (!is_pin_set(io_data, GPIO_BIT(BTN1))) delay_ms = 100;  // GPIO27

        gpio_set_level(LED, led_state);
        led_state = !led_state;

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
