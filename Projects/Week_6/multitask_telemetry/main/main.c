#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"

#define LED_PWM GPIO_NUM_2
#define LED_RED GPIO_NUM_12
#define LED_YELLOW GPIO_NUM_27
#define LED_GREEN GPIO_NUM_14


// Task 1
#define PRIO_BREATHING 2
#define PRIO_TRAFFIC   4

// Task 2
// #define PRIO_TRAFFIC 5

// Task 3
// #define PRIO_TELEMETRY 4

// #define PRIO_BREATHING 4
// #define PRIO_TRAFFIC 2
#define PRIO_TELEMETRY 1

#define STACK_BREATHING 2048
#define STACK_TRAFFIC 2048
#define STACK_TELEMETRY 2048

#if CONFIG_FREERTOS_UNICORE
static const BaseType_t core_id = 0;
#else
static const BaseType_t core_id = 1;
#endif

typedef enum
{
    TL_RED,
    TL_YELLOW,
    TL_GREEN
} traffic_light_state_t;

static volatile traffic_light_state_t traffic_state = TL_RED;

static TaskHandle_t breathing_handle = NULL;
static TaskHandle_t traffic_handle = NULL;
static TaskHandle_t telemetry_handle = NULL;

static void breathing_led_task(void *pvParameters)
{
    (void)pvParameters;

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t led_cfg = {
        .gpio_num = LED_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .intr_type = LEDC_INTR_DISABLE};
    ledc_channel_config(&led_cfg);

    int duty = 0;
    int step = 50;
    const int max_duty = (1 << 13) - 1;

    while (1)
    {
        duty += step;

        if (duty >= max_duty)
        {
            duty = max_duty;
            step = -step;
        }
        else if (duty <= 0)
        {
            duty = 0;
            step = -step;
        }

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    vTaskDelete(NULL);
}

static void traffic_light_task(void *pvParameters)
{
    (void)pvParameters;

    gpio_set_direction(LED_RED, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_YELLOW, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_GREEN, GPIO_MODE_OUTPUT);

    while (1)
    {
        traffic_state = TL_RED;
        gpio_set_level(LED_RED, 1);
        gpio_set_level(LED_YELLOW, 0);
        gpio_set_level(LED_GREEN, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));

        traffic_state = TL_YELLOW;
        gpio_set_level(LED_RED, 0);
        gpio_set_level(LED_YELLOW, 1);
        gpio_set_level(LED_GREEN, 0);
        vTaskDelay(pdMS_TO_TICKS(500));

        traffic_state = TL_GREEN;
        gpio_set_level(LED_RED, 0);
        gpio_set_level(LED_YELLOW, 0);
        gpio_set_level(LED_GREEN, 1);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    vTaskDelete(NULL);
}

static void telemetry_task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        printf("\n===== TELEMETRY =====\n");
        printf("Uptime: %lld ms\n", (long long)(esp_timer_get_time() / 1000));

        const char *tstate =
            (traffic_state == TL_RED) ? "RED" : (traffic_state == TL_YELLOW) ? "YELLOW"
                                                                             : "GREEN";

        printf("Traffic light: %s\n", tstate);

        printf("Task: breathing | stack free: %4u | prio: %u | core: %d\n",
               (unsigned)uxTaskGetStackHighWaterMark(breathing_handle),
               (unsigned)uxTaskPriorityGet(breathing_handle),
               xTaskGetCoreID(breathing_handle));

        printf("Task: traffic   | stack free: %4u | prio: %u | core: %d\n",
               (unsigned)uxTaskGetStackHighWaterMark(traffic_handle),
               (unsigned)uxTaskPriorityGet(traffic_handle),
               xTaskGetCoreID(traffic_handle));

        printf("Task: telemetry | stack free: %4u | prio: %u | core: %d\n",
               (unsigned)uxTaskGetStackHighWaterMark(telemetry_handle),
               (unsigned)uxTaskPriorityGet(telemetry_handle),
               xTaskGetCoreID(telemetry_handle));

        printf("======================\n");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreatePinnedToCore(breathing_led_task, "breathing",
                            STACK_BREATHING, NULL, PRIO_BREATHING,
                            &breathing_handle, core_id);

    xTaskCreatePinnedToCore(traffic_light_task, "traffic",
                            STACK_TRAFFIC, NULL, PRIO_TRAFFIC,
                            &traffic_handle, core_id);

    xTaskCreatePinnedToCore(telemetry_task, "telemetry",
                            STACK_TELEMETRY, NULL, PRIO_TELEMETRY,
                            &telemetry_handle, core_id);
}