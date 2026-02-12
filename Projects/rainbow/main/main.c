/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/
#include <math.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE // для Wokwi надежнее
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT
#define LEDC_FREQ_HZ 4000

#define CH_R LEDC_CHANNEL_0
#define CH_G LEDC_CHANNEL_1
#define CH_B LEDC_CHANNEL_2

#define GPIO_R GPIO_NUM_26
#define GPIO_G GPIO_NUM_25
#define GPIO_B GPIO_NUM_33

#define DUTY_MAX ((1 << LEDC_DUTY_RES) - 1)
#define RGB_TO_DUTY(x) ((uint32_t)((x) * DUTY_MAX / 255))

static void pwm_init(void)
{
  ledc_timer_config_t t = {
      .speed_mode = LEDC_MODE,
      .timer_num = LEDC_TIMER,
      .duty_resolution = LEDC_DUTY_RES,
      .freq_hz = LEDC_FREQ_HZ,
      .clk_cfg = LEDC_AUTO_CLK};
  ESP_ERROR_CHECK(ledc_timer_config(&t));

  ledc_channel_config_t ch = {
      .speed_mode = LEDC_MODE,
      .timer_sel = LEDC_TIMER,
      .intr_type = LEDC_INTR_DISABLE,
      .duty = 0,
      .hpoint = 0};

  ch.channel = CH_R;
  ch.gpio_num = GPIO_R;
  ESP_ERROR_CHECK(ledc_channel_config(&ch));
  ch.channel = CH_G;
  ch.gpio_num = GPIO_G;
  ESP_ERROR_CHECK(ledc_channel_config(&ch));
  ch.channel = CH_B;
  ch.gpio_num = GPIO_B;
  ESP_ERROR_CHECK(ledc_channel_config(&ch));
}

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, CH_R, RGB_TO_DUTY(r)));
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, CH_G, RGB_TO_DUTY(g)));
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, CH_B, RGB_TO_DUTY(b)));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, CH_R));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, CH_G));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, CH_B));
}

static void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b)
{
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;

  float rf = 0, gf = 0, bf = 0;
  if (h < 60)
  {
    rf = c;
    gf = x;
    bf = 0;
  }
  else if (h < 120)
  {
    rf = x;
    gf = c;
    bf = 0;
  }
  else if (h < 180)
  {
    rf = 0;
    gf = c;
    bf = x;
  }
  else if (h < 240)
  {
    rf = 0;
    gf = x;
    bf = c;
  }
  else if (h < 300)
  {
    rf = x;
    gf = 0;
    bf = c;
  }
  else
  {
    rf = c;
    gf = 0;
    bf = x;
  }

  *r = (uint8_t)((rf + m) * 255.0f);
  *g = (uint8_t)((gf + m) * 255.0f);
  *b = (uint8_t)((bf + m) * 255.0f);
}

void app_main(void)
{
  pwm_init();

  float hue = 0.0f;            // 0..360
  const float s = 1.0f;        // насыщенность
  const float v = 0.5f;        // яркость (общая)
  const float hue_step = 1.0f; // скорость переливания (градусов за шаг)
  const int delay_ms = 15;     // плавность

  while (1)
  {
    uint8_t r, g, b;
    hsv_to_rgb(hue, s, v, &r, &g, &b);
    set_rgb(r, g, b);

    hue += hue_step;
    if (hue >= 360.0f)
      hue -= 360.0f;

    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}
