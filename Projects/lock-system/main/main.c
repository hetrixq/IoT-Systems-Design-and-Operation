// Бригада №3
// Павлов Аркадий, Малков Максим, Авдеев Евгений

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Макрос для перевода номера GPIO в битовую маску
#define GPIO_BIT(gpio_num) (1ULL << (gpio_num))

// Пины (как у тебя)
#define LED   GPIO_NUM_13

#define BTN1  26
#define BTN2  27
#define BTN3  14
#define BTN4  12

// Пин релe
#define RELAY GPIO_NUM_15

#define OPEN_TIME_MS     10000   // дверь открыта 10 секунд
#define BLINK_PERIOD_MS  300     // мигание во время открытия
#define DEBOUNCE_MS      40      // антидребезг
#define INPUT_TIMEOUT_MS 5000    // сброс ввода, если пауза между нажатиями

// Пароль (код) для открытия двери
static const int CODE[] = {1,2,3,4};
#define CODE_LEN (sizeof(CODE)/sizeof(CODE[0]))

#define RELAY_ACTIVE_LOW 1

static inline int relay_on_level(void)  { return RELAY_ACTIVE_LOW ? 0 : 1; }
static inline int relay_off_level(void) { return RELAY_ACTIVE_LOW ? 1 : 0; }

static inline bool btn_pressed(gpio_num_t pin)
{
    return gpio_get_level(pin) == 0;
}

// Ждём стабилизации (антидребезг) и подтверждаем нажатие
static bool confirm_press(gpio_num_t pin)
{
    if (!btn_pressed(pin)) return false;
    vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
    return btn_pressed(pin);
}

// Ждём отпускания кнопки (чтобы одно нажатие считалось один раз)
static void wait_release(gpio_num_t pin)
{
    while (btn_pressed(pin)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
}

static int read_key_event(void)
{
    if (confirm_press(BTN1)) { wait_release(BTN1); return 1; }
    if (confirm_press(BTN2)) { wait_release(BTN2); return 2; }
    if (confirm_press(BTN3)) { wait_release(BTN3); return 3; }
    if (confirm_press(BTN4)) { wait_release(BTN4); return 4; }
    return 0;
}

static void set_locked_indicator(void)
{
    // дверь закрыта: LED горит постоянно
    gpio_set_level(LED, 1);
}

static void open_door_sequence(void)
{
    // Включить реле (открыть)
    gpio_set_level(RELAY, relay_on_level());

    // 10 секунд мигаем LED
    const int steps = OPEN_TIME_MS / BLINK_PERIOD_MS;
    for (int i = 0; i < steps; i++) {
        gpio_set_level(LED, (i % 2) ? 1 : 0);
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
    }

    // Выключить реле (закрыть)
    gpio_set_level(RELAY, relay_off_level());

    // Вернуться в режим "закрыто"
    set_locked_indicator();
}

void app_main(void)
{
    // Кнопки: вход + подтяжка вверх
    gpio_config_t btn_cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = GPIO_BIT(BTN1) | GPIO_BIT(BTN2) | GPIO_BIT(BTN3) | GPIO_BIT(BTN4),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&btn_cfg);

    // LED и реле: выходы
    gpio_set_direction(LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY, GPIO_MODE_OUTPUT);

    // Начальное состояние: дверь закрыта
    gpio_set_level(RELAY, relay_off_level());
    set_locked_indicator();

    int input[CODE_LEN] = {0};
    int idx = 0;

    int64_t last_input_tick = 0;

    while (1) {
        int key = read_key_event();

        // Таймаут ввода
        int64_t now = xTaskGetTickCount();
        if (idx > 0 && (now - last_input_tick) > pdMS_TO_TICKS(INPUT_TIMEOUT_MS)) {
            idx = 0;
            memset(input, 0, sizeof(input));
        }

        if (key != 0) {
            last_input_tick = now;

            input[idx++] = key;
            printf("Key: %d (idx=%d)\n", key, idx);

            if (idx >= (int)CODE_LEN) {
                bool ok = true;
                for (int i = 0; i < (int)CODE_LEN; i++) {
                    if (input[i] != CODE[i]) { ok = false; break; }
                }

                // Сброс ввода в любом случае
                idx = 0;
                memset(input, 0, sizeof(input));

                if (ok) {
                    printf("CODE OK -> OPEN\n");
                    open_door_sequence();
                } else {
                    printf("CODE FAIL\n");
                    set_locked_indicator();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
