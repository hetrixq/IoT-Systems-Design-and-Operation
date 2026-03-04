

#include <stdio.h>
#include <stdbool.h>
#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Пины подключения светодиодов
#define LED_RED GPIO_NUM_12
#define LED_YELLOW GPIO_NUM_27
#define LED_GREEN GPIO_NUM_14
#define LED_BUILTIN GPIO_NUM_2

// Именованные приоритеты для задач
#define TASK_PRIO_BLINK (tskIDLE_PRIORITY + 2)

// Стек задач
#define TASK_STACK_WORDS 1024

// Прототипы задач
static void task_blink_1(void *pvParameters);
static void task_blink_2(void *pvParameters);
static void task_blink_3(void *pvParameters);
static void task_blink_4(void *pvParameters);

void app_main(void)
{
    printf("Blinking tasks\n");

    // Конфигурируем GPIO
    gpio_set_direction(LED_RED, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_YELLOW, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);

    gpio_set_level(LED_RED, 0);
    gpio_set_level(LED_YELLOW, 0);
    gpio_set_level(LED_GREEN, 0);
    gpio_set_level(LED_BUILTIN, 0);

    // Создаем задачи
    xTaskCreate(task_blink_1, "Blink1", TASK_STACK_WORDS, NULL, TASK_PRIO_BLINK, NULL);
    xTaskCreate(task_blink_2, "Blink2", TASK_STACK_WORDS, NULL, TASK_PRIO_BLINK, NULL);
    xTaskCreate(task_blink_3, "Blink3", TASK_STACK_WORDS, NULL, TASK_PRIO_BLINK, NULL);
    xTaskCreate(task_blink_4, "Blink4", TASK_STACK_WORDS, NULL, TASK_PRIO_BLINK, NULL);
}

static void task_blink_1(void *pvParameters)
{
    (void)pvParameters;
    bool led = false;
    while (true)
    {
        led = !led;
        gpio_set_level(LED_RED, led);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void task_blink_2(void *pvParameters)
{
    (void)pvParameters;
    bool led = false;
    while (true)
    {
        led = !led;
        gpio_set_level(LED_YELLOW, led);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void task_blink_3(void *pvParameters)
{
    (void)pvParameters;
    bool led = false;
    while (true)
    {
        led = !led;
        gpio_set_level(LED_GREEN, led);
        vTaskDelay(pdMS_TO_TICKS(167));
    }
}

static void task_blink_4(void *pvParameters)
{
    (void)pvParameters;
    bool led = false;
    while (true)
    {
        led = !led;
        gpio_set_level(LED_BUILTIN, led);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}