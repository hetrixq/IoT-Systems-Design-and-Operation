#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <stdio.h>
#include <stdbool.h>

#define LED_GPIO   GPIO_NUM_13   // D13
#define RELAY_GPIO GPIO_NUM_15   // D15

#define RELAY_ACTIVE_LOW 1

static inline void relay_set(bool on) {
#if RELAY_ACTIVE_LOW
  gpio_set_level(RELAY_GPIO, on ? 0 : 1);
#else
  gpio_set_level(RELAY_GPIO, on ? 1 : 0);
#endif
}

static inline bool relay_get_on(void) {
#if RELAY_ACTIVE_LOW
  return gpio_get_level(RELAY_GPIO) == 0;
#else
  return gpio_get_level(RELAY_GPIO) == 1;
#endif
}

void app_main(void) {
  gpio_reset_pin(LED_GPIO);
  gpio_reset_pin(RELAY_GPIO);

  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_direction(RELAY_GPIO, GPIO_MODE_OUTPUT);

  bool ledState   = false;
  bool relayState = false;

  gpio_set_level(LED_GPIO, ledState);
  relay_set(relayState);

  printf("System started\n");
  printf("[RELAY] OFF -> LED = 1 Hz\n");

  TickType_t lastRelayToggle = xTaskGetTickCount();
  TickType_t lastLedToggle   = xTaskGetTickCount();

  while (true) {
    TickType_t now = xTaskGetTickCount();

    // --- Реле: переключаем раз в 10 секунд ---
    if (now - lastRelayToggle >= pdMS_TO_TICKS(10000)) {
      lastRelayToggle = now;
      relayState = !relayState;
      relay_set(relayState);

      printf(
        "[RELAY] %s -> LED = %s\n",
        relayState ? "ON" : "OFF",
        relayState ? "5 Hz" : "1 Hz"
      );
    }

    // --- Светодиод: 1 Гц или 5 Гц ---
    TickType_t ledPeriod = relayState
        ? pdMS_TO_TICKS(100)   // 5 Гц
        : pdMS_TO_TICKS(500);  // 1 Гц

    if (now - lastLedToggle >= ledPeriod) {
      lastLedToggle = now;
      ledState = !ledState;
      gpio_set_level(LED_GPIO, ledState);

      printf("[LED] %s\n", ledState ? "ON" : "OFF");
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
