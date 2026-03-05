/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#define BTN_GPIO GPIO_NUM_2
#define DEBOUNCE_MS 50

#define LED1_GPIO GPIO_NUM_25
#define LED2_GPIO GPIO_NUM_26
#define LED3_GPIO GPIO_NUM_27
#define LED4_GPIO GPIO_NUM_33

typedef struct
{
  uint32_t gpio_num;
} button_event_t;

static QueueHandle_t button_event_queue;

static void IRAM_ATTR button_isr_handler(void *arg)
{
  button_event_t evt = {.gpio_num = (uint32_t)arg};
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(button_event_queue, &evt, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void set_leds(uint8_t idx)
{
  gpio_set_level(LED1_GPIO, idx == 0);
  gpio_set_level(LED2_GPIO, idx == 1);
  gpio_set_level(LED3_GPIO, idx == 2);
  gpio_set_level(LED4_GPIO, idx == 3);
}

static void button_task(void *arg)
{
  (void)arg;

  button_event_t evt;
  TickType_t last_press_time = 0;
  uint8_t idx = 0;

  set_leds(idx);

  while (1)
  {
    if (xQueueReceive(button_event_queue, &evt, portMAX_DELAY))
    {
      TickType_t now = xTaskGetTickCount();

      if ((now - last_press_time) * portTICK_PERIOD_MS < DEBOUNCE_MS)
      {
        continue;
      }

      if (gpio_get_level(BTN_GPIO) != 0)
      {
        continue;
      }

      last_press_time = now;

      idx = (idx + 1) & 0x03;
      set_leds(idx);
      printf("Button event, LED index = %u\n", (unsigned)idx);

      while (gpio_get_level(BTN_GPIO) == 0)
      {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
  }
}

void app_main(void)
{
  printf("ISR + Queue + Debounce (DEBOUNCE_MS=%d) — sequential LEDs\n", DEBOUNCE_MS);

  gpio_set_direction(LED1_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_direction(LED2_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_direction(LED3_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_direction(LED4_GPIO, GPIO_MODE_OUTPUT);

  gpio_config_t cfg = {
      .pin_bit_mask = (1ULL << BTN_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE};
  gpio_config(&cfg);

  button_event_queue = xQueueCreate(10, sizeof(button_event_t));

  gpio_install_isr_service(0);
  gpio_isr_handler_add(BTN_GPIO, button_isr_handler, (void *)BTN_GPIO);

  xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);
}