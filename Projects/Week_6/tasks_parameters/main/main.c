#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LED_RED GPIO_NUM_12
#define LED_YELLOW GPIO_NUM_27
#define LED_GREEN GPIO_NUM_14

#define TASK_STACK_WORDS 1024
#define TASK_PRIO_BLINK (tskIDLE_PRIORITY + 2)

// Единая функция-задача. Параметр: 1/2/3.
static void task_blink(void *pvParameters)
{
    // Достаём целое из указателя
    const int id = (int)(intptr_t)pvParameters;

    gpio_num_t gpio;
    uint32_t delay_ms;

    switch (id)
    {
    case 1:
        gpio = LED_RED;
        delay_ms = 500;
        break; // 1 Гц
    case 2:
        gpio = LED_YELLOW;
        delay_ms = 250;
        break; // 2 Гц
    case 3:
        gpio = LED_GREEN;
        delay_ms = 167;
        break; // 3 Гц
    default:
        vTaskDelete(NULL);
        return;
    }

    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    bool led = false;

    while (true)
    {
        led = !led;
        gpio_set_level(gpio, led);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    printf("Blinking with integer task parameter\n");

    // Передаём “ID” прямо как число, упакованное в void*
    xTaskCreate(task_blink, "R Blink", TASK_STACK_WORDS, (void *)(intptr_t)1, TASK_PRIO_BLINK, NULL);
    xTaskCreate(task_blink, "Y Blink", TASK_STACK_WORDS, (void *)(intptr_t)2, TASK_PRIO_BLINK, NULL);
    xTaskCreate(task_blink, "G Blink", TASK_STACK_WORDS, (void *)(intptr_t)3, TASK_PRIO_BLINK, NULL);
}