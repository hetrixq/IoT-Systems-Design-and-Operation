/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

#define TEMP_MIN 10.0f
#define TEMP_MAX 30.0f
#define HUMIDITY_MAX 70.0f

#define DHT_PIN GPIO_NUM_21
#define LED_PIN GPIO_NUM_5
#define BUTTON_PIN GPIO_NUM_15

#define LOOP_PERIOD_MS 100
#define LOOP_ITERATIONS 10
#define ITER_READ_SENSOR 5
#define ITER_PRINT 10
static float g_temperature = 0.0f;
static float g_humidity = 0.0f;
static bool g_alarm = false;
static char g_alarm_msg[256] = "";
static bool g_led_state = false;
static int g_iteration = 0;

static bool dht22_read(float *out_temp, float *out_hum)
{
    uint8_t data[5] = {0};

    gpio_set_direction(DHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_PIN, 0);
    ets_delay_us(1200);
    gpio_set_level(DHT_PIN, 1);
    ets_delay_us(30);
    gpio_set_direction(DHT_PIN, GPIO_MODE_INPUT);

    int timeout;

    timeout = 0;
    while (gpio_get_level(DHT_PIN) == 1)
    {
        ets_delay_us(1);
        if (++timeout > 100)
            return false;
    }
    timeout = 0;
    while (gpio_get_level(DHT_PIN) == 0)
    {
        ets_delay_us(1);
        if (++timeout > 120)
            return false;
    }
    timeout = 0;
    while (gpio_get_level(DHT_PIN) == 1)
    {
        ets_delay_us(1);
        if (++timeout > 120)
            return false;
    }

    for (int i = 0; i < 40; i++)
    {
        timeout = 0;
        while (gpio_get_level(DHT_PIN) == 0)
        {
            ets_delay_us(1);
            if (++timeout > 70)
                return false;
        }
        ets_delay_us(35);
        int bit = gpio_get_level(DHT_PIN);
        data[i / 8] = (uint8_t)((data[i / 8] << 1) | bit);
        timeout = 0;
        while (gpio_get_level(DHT_PIN) == 1)
        {
            ets_delay_us(1);
            if (++timeout > 100)
                return false;
        }
    }

    uint8_t csum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (csum != data[4])
        return false;

    uint16_t raw_hum = ((uint16_t)data[0] << 8) | data[1];
    uint16_t raw_temp = ((uint16_t)(data[2] & 0x7F) << 8) | data[3];
    *out_hum = raw_hum / 10.0f;
    *out_temp = raw_temp / 10.0f;
    if (data[2] & 0x80)
        *out_temp = -*out_temp;

    return true;
}

static void peripherals_init(void)
{
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    gpio_reset_pin(DHT_PIN);
    gpio_set_direction(DHT_PIN, GPIO_MODE_INPUT);

    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
}

void app_main(void)
{
    peripherals_init();

    printf("\n========================================\n");
    printf("  Pharma Warehouse - Climate Monitor\n");
    printf("  Normal: T=[%.0f..%.0f]C, RH<=%.0f%%\n",
           TEMP_MIN, TEMP_MAX, HUMIDITY_MAX);
    printf("========================================\n\n");

    while (1)
    {
        g_iteration++;
        if (g_iteration > LOOP_ITERATIONS)
            g_iteration = 1;

        if (g_alarm)
        {
            g_led_state = !g_led_state;
            gpio_set_level(LED_PIN, g_led_state ? 1 : 0);
        }
        else
        {
            if (g_iteration == 1)
            {
                g_led_state = !g_led_state;
                gpio_set_level(LED_PIN, g_led_state ? 1 : 0);
            }
        }

        if (g_iteration == ITER_READ_SENSOR)
        {
            float t, h;
            if (dht22_read(&t, &h))
            {
                g_temperature = t;
                g_humidity = h;
            }
            else
            {
                printf("[WARNING] DHT22 read failed, retrying next cycle\n");
            }
        }

        if (g_iteration == ITER_PRINT)
        {
            printf("[DATA] T=%.1fC  RH=%.1f%%\n", g_temperature, g_humidity);

            bool prev_alarm = g_alarm;
            g_alarm = false;
            g_alarm_msg[0] = '\0';

            if (g_temperature < TEMP_MIN)
            {
                snprintf(g_alarm_msg, sizeof(g_alarm_msg),
                         "!!! ALARM: temperature %.1fC is below minimum (%.0fC) !!!",
                         g_temperature, TEMP_MIN);
                g_alarm = true;
            }
            else if (g_temperature > TEMP_MAX)
            {
                snprintf(g_alarm_msg, sizeof(g_alarm_msg),
                         "!!! ALARM: temperature %.1fC exceeds maximum (%.0fC) !!!",
                         g_temperature, TEMP_MAX);
                g_alarm = true;
            }

            if (g_humidity > HUMIDITY_MAX)
            {
                char buf[128];
                snprintf(buf, sizeof(buf),
                         "!!! ALARM: humidity %.1f%% exceeds maximum (%.0f%%) !!!",
                         g_humidity, HUMIDITY_MAX);
                if (g_alarm)
                {
                    strncat(g_alarm_msg, " | ", sizeof(g_alarm_msg) - strlen(g_alarm_msg) - 1);
                    strncat(g_alarm_msg, buf, sizeof(g_alarm_msg) - strlen(g_alarm_msg) - 1);
                }
                else
                {
                    strncpy(g_alarm_msg, buf, sizeof(g_alarm_msg) - 1);
                    g_alarm = true;
                }
            }

            if (g_alarm)
            {
                printf("%s\n", g_alarm_msg);
            }
            else if (prev_alarm)
            {
                printf("[OK] Values returned to normal.\n");
            }
        }

        if (gpio_get_level(BUTTON_PIN) == 0)
        {
            if (g_alarm && g_alarm_msg[0] != '\0')
            {
                printf("[BUTTON] Last alarm message:\n  %s\n", g_alarm_msg);
            }
            else
            {
                printf("[BUTTON] No active alarms.\n");
            }
            while (gpio_get_level(BUTTON_PIN) == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_PERIOD_MS));
    }
}
