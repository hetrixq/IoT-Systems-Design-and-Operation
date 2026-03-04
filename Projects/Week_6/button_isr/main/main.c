#include <stdio.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define BUTTON_GPIO GPIO_NUM_35
#define LED_GPIO GPIO_NUM_2
#define DEBOUNCE_US 200000

static volatile uint32_t button_press_count = 0;
static esp_timer_handle_t debounce_timer;

static void debounce_timer_cb(void *arg)
{
  (void)arg;
  gpio_intr_enable(BUTTON_GPIO);
}

static void IRAM_ATTR button_isr_handler(void *arg)
{
  (void)arg;
  gpio_intr_disable(BUTTON_GPIO);
  button_press_count++;
  esp_timer_stop(debounce_timer);
  esp_timer_start_once(debounce_timer, DEBOUNCE_US);
}

void app_main(void)
{
  /* Конфигурируем кнопку */
  gpio_config_t io_conf = {0};
  io_conf.pin_bit_mask = (1ULL << BUTTON_GPIO);
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.intr_type = GPIO_INTR_NEGEDGE;
  gpio_config(&io_conf);

  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

  /* Создаём однократный таймер дебаунса */
  esp_timer_create_args_t timer_args = {
      .callback = debounce_timer_cb,
      .arg = NULL,
      .name = "debounce"};
  esp_timer_create(&timer_args, &debounce_timer);

  gpio_install_isr_service(0);
  gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);

  while (1)
  {
    printf("Button pressed %lu times\n", (unsigned long)button_press_count);
    gpio_set_level(LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(LED_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}