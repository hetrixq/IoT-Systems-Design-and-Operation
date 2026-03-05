#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define LED GPIO_NUM_13
#define BOOT_BTN GPIO_NUM_35

esp_timer_handle_t periodic_timer;
esp_timer_handle_t once_timer;

// Периодический таймер: переключает LED
static void periodic_timer_callback(void *arg)
{
  gpio_set_level(LED, !gpio_get_level(LED));
}

// Однократный таймер: останавливает периодический через 15 секунд
static void once_timer_callback(void *arg)
{
  esp_timer_stop(periodic_timer);
  gpio_set_level(LED, 0);
}

// Прерывание от кнопки BOOT: перезапускает периодический таймер с частотой 2 Гц
static void IRAM_ATTR boot_btn_isr_handler(void *arg)
{
  // Останавливаем текущий периодический таймер (если запущен)
  esp_timer_stop(periodic_timer);
  // Запускаем с периодом 250 мс (2 Гц)
  esp_timer_start_periodic(periodic_timer, 250000);
}

void app_main(void)
{
  // --- Настройка LED ---
  gpio_reset_pin(LED);
  gpio_set_direction(LED, GPIO_MODE_INPUT_OUTPUT);

  gpio_reset_pin(BOOT_BTN);
  gpio_set_direction(BOOT_BTN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(BOOT_BTN, GPIO_PULLUP_ONLY);
  gpio_set_intr_type(BOOT_BTN, GPIO_INTR_NEGEDGE); // срабатывание по спаду
  gpio_install_isr_service(0);
  gpio_isr_handler_add(BOOT_BTN, boot_btn_isr_handler, NULL);

  // --- Создание и запуск таймеров ---
  const esp_timer_create_args_t periodic_timer_args = {
      .callback = &periodic_timer_callback,
      .name = "periodic"};

  const esp_timer_create_args_t once_timer_args = {
      .callback = &once_timer_callback,
      .name = "once"};

  esp_timer_create(&periodic_timer_args, &periodic_timer);
  esp_timer_start_periodic(periodic_timer, 500000); // 1 Гц (период 500 мс)

  esp_timer_create(&once_timer_args, &once_timer);
  esp_timer_start_once(once_timer, 15000000); // остановить через 15 секунд
}