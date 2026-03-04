#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

// Пины кнопок (физический порядок слева направо в Wokwi)
#define BTN1_GPIO GPIO_NUM_27 // 1-я (левая)  wokwi-btn3 -> D27 — ШИМ
#define BTN2_GPIO GPIO_NUM_14 // 2-я           wokwi-btn2 -> D14 — время
#define BTN3_GPIO GPIO_NUM_26 // 3-я           wokwi-btn1 -> D26 — LED
#define BTN4_GPIO GPIO_NUM_13 // 4-я (правая)  wokwi-btn4 -> D13 — телеметрия

// Внешний LED (D15, 220 Ом на GND)
#define LED_GPIO GPIO_NUM_15

// ШИМ - "нагреватель"
#define HEATER_GPIO GPIO_NUM_33
#define LEDC_TIMER_RESOLUTION LEDC_TIMER_8_BIT

#define DEBOUNCE_MS 50 // порог подавления дребезга в мс
#define QUEUE_LEN 10   // длина очереди сообщений

// Тип события
typedef enum
{
  BTN_EVT_PRESS = 1,
  BTN_EVT_RELEASE
} btn_event_type_t;

// Событие
typedef struct
{
  uint8_t btn_id;        // 1,2,3,4
  btn_event_type_t type; // тип события (PRESS или RELEASE)
  TickType_t tick;       // время события (в тиках FreeRTOS)
} button_event_t;

// Очередь сообщений
static QueueHandle_t btn_evt_queue = NULL;

// Мьютекс для задачи ШИМ
static SemaphoreHandle_t pwm_mutex = NULL;

// Состояние кнопки
typedef struct
{
  TickType_t last_press_tick;
  TickType_t last_release_tick;
  bool is_pressed;
} button_state_t;

static button_state_t button_state[4];

// Область данных для обмена защищёнными значениями
typedef struct
{
  double power;
} data_area_t;

data_area_t data = {
    .power = 0.0,
};

// Хэндл задачи buttons_task для телеметрии
static TaskHandle_t buttons_task_handle = NULL;

// Счётчик обработанных событий (для телеметрии)
static volatile uint32_t events_processed = 0;

// Текущий индекс мощности (для телеметрии)
static volatile int curr_level_index_global = 0;

// Вспомогательная функция для конвертации GPIO -> id
static inline uint8_t gpio_to_btn_id(gpio_num_t gpio)
{
  if (gpio == BTN1_GPIO)
    return 1;
  if (gpio == BTN2_GPIO)
    return 2;
  if (gpio == BTN3_GPIO)
    return 3;
  if (gpio == BTN4_GPIO)
    return 4;
  return 0xFF;
}

// ISR обработчик — только упаковывает событие и шлёт в очередь
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
  gpio_num_t gpio_num = (gpio_num_t)(intptr_t)arg;

  uint8_t id = gpio_to_btn_id(gpio_num);
  if (id == 0xFF)
    return;

  int level = gpio_get_level(gpio_num); // 0=pressed, 1=released

  button_event_t evt;
  evt.btn_id = id;
  evt.type = (level == 0 ? BTN_EVT_PRESS : BTN_EVT_RELEASE);
  evt.tick = xTaskGetTickCountFromISR();

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(btn_evt_queue, &evt, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken)
  {
    portYIELD_FROM_ISR();
  }
}

/* Задача обработки кнопочных событий. Подавление дребезга + логика:
   - Кнопка 1: циклически меняет мощность нагревателя (ШИМ)
   - Кнопка 2: фиксирует время нажатия кнопки
   - Кнопка 3: зажигает встроенный LED на 150 мс
   - Кнопка 4: выводит телеметрию о задаче buttons_task()
*/
static void buttons_task(void *arg)
{
  (void)arg;
  button_event_t evt;
  const TickType_t debounce_ticks = pdMS_TO_TICKS(DEBOUNCE_MS);

  // Время нажатия кнопки 2
  TickType_t press_start_tick_btn2 = 0;

  // Значения "мощности нагревателя"
  const int power_levels[] = {0, 25, 50, 75, 100};
  const int n_levels = sizeof(power_levels) / sizeof(power_levels[0]);
  int curr_level_index = 0;

  // Создание мьютекса
  pwm_mutex = xSemaphoreCreateMutex();

  while (1)
  {
    // Ждём сообщения из очереди
    if (!xQueueReceive(btn_evt_queue, &evt, portMAX_DELAY))
      continue;

    events_processed++;

    button_state_t *state = &button_state[evt.btn_id - 1];
    TickType_t now = evt.tick;

    // Debounce PRESS
    if (evt.type == BTN_EVT_PRESS)
    {
      if (now - state->last_press_tick < debounce_ticks)
        continue; // игнорируем дребезг
      state->last_press_tick = now;

      if (!state->is_pressed)
      {
        state->is_pressed = true;
        printf("BTN%d PRESSED\n", evt.btn_id);

        if (evt.btn_id == gpio_to_btn_id(BTN1_GPIO))
        {
          // Меняем мощность циклически
          curr_level_index = (curr_level_index + 1) % n_levels;
          curr_level_index_global = curr_level_index;
          int power = power_levels[curr_level_index];
          printf("BTN1 action: Heater power set to %d%%\n", power);
          xSemaphoreTake(pwm_mutex, portMAX_DELAY);
          data.power = (double)power;
          xSemaphoreGive(pwm_mutex);
        }
        else if (evt.btn_id == gpio_to_btn_id(BTN2_GPIO))
        {
          // Фиксируем момент нажатия
          press_start_tick_btn2 = now;
        }
        else if (evt.btn_id == gpio_to_btn_id(BTN3_GPIO))
        {
          // Вспышка встроенным светодиодом
          printf("BTN3 action: blink LED\n");
          gpio_set_level(LED_GPIO, 1);
          vTaskDelay(pdMS_TO_TICKS(150));
          gpio_set_level(LED_GPIO, 0);
        }
        else if (evt.btn_id == gpio_to_btn_id(BTN4_GPIO))
        {
          // Телеметрия buttons_task
          UBaseType_t watermark = uxTaskGetStackHighWaterMark(buttons_task_handle);
          UBaseType_t queue_waiting = uxQueueMessagesWaiting(btn_evt_queue);
          UBaseType_t queue_spaces = uxQueueSpacesAvailable(btn_evt_queue);

          xSemaphoreTake(pwm_mutex, portMAX_DELAY);
          double cur_power = data.power;
          xSemaphoreGive(pwm_mutex);

          printf("=== buttons_task telemetry ===\n");
          printf("  Stack high watermark : %u words free\n", (unsigned)watermark);
          printf("  Events processed     : %lu\n", (unsigned long)events_processed);
          printf("  Queue msgs waiting   : %u\n", (unsigned)queue_waiting);
          printf("  Queue spaces free    : %u\n", (unsigned)queue_spaces);
          printf("  Heater power         : %.1f%%\n", cur_power);
          printf("  Power level index    : %d / %d\n", curr_level_index, n_levels - 1);
          printf("  BTN1 pressed now     : %s\n", button_state[0].is_pressed ? "yes" : "no");
          printf("  BTN2 pressed now     : %s\n", button_state[1].is_pressed ? "yes" : "no");
          printf("  BTN3 pressed now     : %s\n", button_state[2].is_pressed ? "yes" : "no");
          printf("  BTN4 pressed now     : %s\n", button_state[3].is_pressed ? "yes" : "no");
          printf("==============================\n");
        }
      }
    }
    // Debounce RELEASE
    else
    {
      if (now - state->last_release_tick < debounce_ticks)
        continue;
      state->last_release_tick = now;

      if (state->is_pressed)
      {
        state->is_pressed = false;
        printf("BTN%d RELEASED\n", evt.btn_id);

        // Длительность удержания кнопки 2
        if (evt.btn_id == gpio_to_btn_id(BTN2_GPIO) && press_start_tick_btn2 != 0)
        {
          TickType_t dt = now - press_start_tick_btn2;
          uint32_t ms = dt * portTICK_PERIOD_MS;
          printf("BTN2 press duration: %lu ms\n", (unsigned long)ms);
          press_start_tick_btn2 = 0;
        }
      }
    }
  }
  vTaskDelete(NULL);
}

// Задача управления ШИМ
static void pwm_task(void *arg)
{
  (void)arg;

  // Настройка таймера LEDC
  ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .timer_num = LEDC_TIMER_0,
      .duty_resolution = LEDC_TIMER_RESOLUTION,
      .freq_hz = 5000,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&timer);

  ledc_channel_config_t ch = {
      .channel = LEDC_CHANNEL_0,
      .gpio_num = HEATER_GPIO,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .timer_sel = LEDC_TIMER_0,
      .duty = 0,
      .hpoint = 0,
  };
  ledc_channel_config(&ch);

  while (1)
  {
    // Захватить мьютекс и считать мощность
    xSemaphoreTake(pwm_mutex, portMAX_DELAY);
    float duty = (float)data.power;
    xSemaphoreGive(pwm_mutex);

    // Обновить ШИМ
    uint32_t duty_val = ((1UL << LEDC_TIMER_RESOLUTION) - 1) * duty / 100.0f;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_val);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    vTaskDelay(pdMS_TO_TICKS(10));
  }
  vTaskDelete(NULL);
}

void app_main(void)
{
  // Создаём очередь событий
  btn_evt_queue = xQueueCreate(QUEUE_LEN, sizeof(button_event_t));
  if (btn_evt_queue == NULL)
  {
    printf("Failed to create queue\n");
    abort();
  }

  // Инициализируем кнопки 1-4 с прерываниями по обоим фронтам
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_ANYEDGE,
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = (1ULL << BTN1_GPIO) |
                      (1ULL << BTN2_GPIO) |
                      (1ULL << BTN3_GPIO) |
                      (1ULL << BTN4_GPIO),
      .pull_up_en = GPIO_PULLUP_ENABLE,  // внутренние pull-up (кнопки на GND, без внешних R)
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
  };
  esp_err_t rc = gpio_config(&io_conf);
  if (rc != ESP_OK)
  {
    printf("gpio_config failed: %d\n", rc);
    abort();
  }

  // Встроенный LED как выход
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(LED_GPIO, 0);

  // Установить ISR service (векторный)
  rc = gpio_install_isr_service(0);
  if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE)
  {
    printf("gpio_install_isr_service failed: %d\n", rc);
    abort();
  }

  // Привязать обработчики ко всем четырём кнопкам
  gpio_isr_handler_add(BTN1_GPIO, gpio_isr_handler, (void *)(intptr_t)BTN1_GPIO);
  gpio_isr_handler_add(BTN2_GPIO, gpio_isr_handler, (void *)(intptr_t)BTN2_GPIO);
  gpio_isr_handler_add(BTN3_GPIO, gpio_isr_handler, (void *)(intptr_t)BTN3_GPIO);
  gpio_isr_handler_add(BTN4_GPIO, gpio_isr_handler, (void *)(intptr_t)BTN4_GPIO);

  // Создаём задачу обработки кнопок (повышенный приоритет)
  xTaskCreate(buttons_task, "buttons_task", 4096, NULL, configMAX_PRIORITIES - 5, &buttons_task_handle);
  // Создаём задачу управления ШИМ (низкий приоритет)
  xTaskCreate(pwm_task, "pwm_task", 2048, NULL, 2, NULL);
}