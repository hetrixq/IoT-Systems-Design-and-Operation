#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define LED     GPIO_NUM_13
#define BUTTON  GPIO_NUM_35

void app_main(void)
{
  gpio_set_direction(LED, GPIO_MODE_OUTPUT);
  gpio_set_direction(BUTTON, GPIO_MODE_INPUT);
  // Включаем встроенную подтяжку к VCC
  gpio_set_pull_mode(BUTTON, GPIO_PULLUP_ONLY);

  uint8_t button_state;
  uint32_t delay_ms;

  while (1) {
    // Считываем состояние кнопки
    button_state = gpio_get_level(BUTTON);
    
    if (button_state == 0) {
      delay_ms = 100;
      printf("Button pressed - 5 Hz blinking\n");
    } else {
      delay_ms = 500;
      printf("Button released - 1 Hz blinking\n");
    }

    // Включаем LED
    gpio_set_level(LED, 1);
    vTaskDelay(delay_ms / portTICK_PERIOD_MS);

    // Выключаем LED
    gpio_set_level(LED, 0);
    vTaskDelay(delay_ms / portTICK_PERIOD_MS);
  }
}