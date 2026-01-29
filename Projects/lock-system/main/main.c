/*
Кейс 1: кодовый замок
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/
#include <stdbool.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GPIO_BIT(gpio_num) (1ULL << (gpio_num))

// Пины GPIO
#define LED GPIO_NUM_13    // Светодиод индикации состояния
#define BTN1 26            // Кнопка 1
#define BTN2 27            // Кнопка 2
#define BTN3 14            // Кнопка 3
#define BTN4 12            // Кнопка 4
#define RELAY GPIO_NUM_15  // Реле управления замком

// Временные константы
#define OPEN_TIME_MS 10000     // Время открытия двери (10 секунд)
#define BLINK_PERIOD_MS 300    // Период мигания светодиода
#define DEBOUNCE_MS 40         // Задержка для устранения дребезга кнопок
#define INPUT_TIMEOUT_MS 5000  // Таймаут сброса ввода (5 секунд)

// Код доступа
static const int CODE[] = {1, 2, 3, 4};
#define CODE_LEN (sizeof(CODE) / sizeof(CODE[0]))

// Конфигурация реле
#define RELAY_LOCKED_LEVEL 0  // Уровень GPIO для блокировки замка

// Проверяет, нажата ли кнопка
static inline bool is_button_pressed(gpio_num_t pin) {
    return gpio_get_level(pin) == 0;
}

// Проверяет нажатие кнопки с защитой от дребезга
static bool debounce_button_press(gpio_num_t pin) {
    if (!is_button_pressed(pin)) return false;
    vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
    return is_button_pressed(pin);
}

// Ожидает отпускания кнопки
static void wait_button_release(gpio_num_t pin) {
    while (is_button_pressed(pin)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
}

// Определяет номер нажатой кнопки (1-4) или возвращает 0
static int get_button_number(void) {
    if (debounce_button_press(BTN1)) {
        wait_button_release(BTN1);
        return 1;
    }
    if (debounce_button_press(BTN2)) {
        wait_button_release(BTN2);
        return 2;
    }
    if (debounce_button_press(BTN3)) {
        wait_button_release(BTN3);
        return 3;
    }
    if (debounce_button_press(BTN4)) {
        wait_button_release(BTN4);
        return 4;
    }
    return 0;
}

// Включает светодиод (дверь закрыта)
static inline void set_locked_led(void) { gpio_set_level(LED, 1); }

// Открывает дверь на 10 секунд с миганием светодиода
static void open_door(void) {
    gpio_set_level(RELAY, !RELAY_LOCKED_LEVEL);

    const int steps = OPEN_TIME_MS / BLINK_PERIOD_MS;
    for (int i = 0; i < steps; i++) {
        gpio_set_level(LED, i & 1);
        vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
    }

    gpio_set_level(RELAY, RELAY_LOCKED_LEVEL);

    set_locked_led();
}

void app_main(void) {
    // Инициализация GPIO
    gpio_config_t btn_cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask =
            GPIO_BIT(BTN1) | GPIO_BIT(BTN2) | GPIO_BIT(BTN3) | GPIO_BIT(BTN4),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&btn_cfg);

    gpio_set_direction(LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY, GPIO_MODE_OUTPUT);

    gpio_set_level(RELAY, RELAY_LOCKED_LEVEL);
    set_locked_led();

    int input[CODE_LEN] = {0};
    int input_digit_index = 0;

    int64_t last_input_tick = 0;

    while (1) {
        int key = get_button_number();

        // Проверка таймаута ввода: сбросить, если долго не было нажатий
        int64_t now = xTaskGetTickCount();
        if (input_digit_index > 0 &&
            (now - last_input_tick) > pdMS_TO_TICKS(INPUT_TIMEOUT_MS)) {
            input_digit_index = 0;
        }

        if (key == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        last_input_tick = now;

        input[input_digit_index++] = key;
        printf("Key: %d (idx=%d)\n", key, input_digit_index);

        if (input_digit_index < (int)CODE_LEN) {
            continue;
        }

        // Проверяем правильность введенного кода
        bool code_ok = true;
        for (int i = 0; i < (int)CODE_LEN; i++) {
            if (input[i] != CODE[i]) {
                code_ok = false;
                break;
            }
        }

        input_digit_index = 0;

        if (code_ok) {
            printf("CODE OK -> OPEN\n");
            open_door();
        } else {
            printf("CODE FAIL\n");
        }
    }
}
