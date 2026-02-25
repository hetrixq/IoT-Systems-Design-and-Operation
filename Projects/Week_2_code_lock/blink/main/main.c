#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define LED GPIO_NUM_2

void app_main(void)
{
  gpio_set_direction(LED, GPIO_MODE_OUTPUT);
  uint32_t led_on = 0;
  printf("LED Blink program started!\n");
  while (true)
  {
    led_on = !led_on;
    gpio_set_level(LED, led_on);
    printf("LED is %s\n", led_on ? "ON" : "OFF");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}