/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#define TAG "SSD1306_DEMO"

// Настройки I2C
#define I2C_MASTER_SCL_IO GPIO_NUM_22
#define I2C_MASTER_SDA_IO GPIO_NUM_21
#define I2C_MASTER_FREQ_HZ 400000
#define I2C_MASTER_NUM I2C_NUM_0

// Настройки дисплея
#define SSD1306_I2C_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Длительность каждого состояния
#define STATE_DURATION_MS 4000
#define BLANK_DURATION_MS 3000

// Состояния автомата
typedef enum
{
  STATE_DEMO_TEXT = 0,  // Состояние 1 – текст
  STATE_DEMO_GRAPHICS,  // Состояние 2 – графика + текст
  STATE_DEMO_ANIMATION, // Состояние 3 – анимация
  STATE_BLANK,          // Состояние 4 – пустой экран
  STATE_COUNT
} display_state_t;

// Простой шрифт 5x8 (каждый символ 5 байт)
static const uint8_t font_5x8[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // }
    {0x08, 0x08, 0x2A, 0x1C, 0x08}, // ->
    {0x08, 0x1C, 0x2A, 0x08, 0x08}  // <-
};

static inline void set_pixel(uint8_t *buffer, int x, int y)
{
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
    return;
  buffer[x + (y / 8) * SCREEN_WIDTH] |= (1 << (y % 8));
}

void draw_char(uint8_t *buffer, char c, int x, int y)
{
  if (c < 32 || c > 126)
    return;
  const uint8_t *char_data = font_5x8[c - 32];
  for (int col = 0; col < 5; col++)
  {
    uint8_t col_data = char_data[col];
    for (int row = 0; row < 8; row++)
    {
      if (col_data & (1 << row))
      {
        int px = x + col, py = y + row;
        if (px < SCREEN_WIDTH && py < SCREEN_HEIGHT)
          buffer[px + (py / 8) * SCREEN_WIDTH] |= (1 << (py % 8));
      }
    }
  }
}

void draw_string(uint8_t *buffer, const char *str, int x, int y)
{
  int cx = x;
  while (*str)
  {
    draw_char(buffer, *str, cx, y);
    cx += 6; // 5 пикс. символ + 1 пробел
    str++;
    if (cx >= SCREEN_WIDTH - 5)
      break;
  }
}

void clear_screen(uint8_t *buffer)
{
  memset(buffer, 0x00, SCREEN_WIDTH * SCREEN_HEIGHT / 8);
}

void clear_rect(uint8_t *buffer, int x, int y, int width, int height)
{
  for (int row = y; row < y + height; row++)
    for (int col = x; col < x + width; col++)
      if (col >= 0 && col < SCREEN_WIDTH && row >= 0 && row < SCREEN_HEIGHT)
        buffer[col + (row / 8) * SCREEN_WIDTH] &= ~(1 << (row % 8));
}

void draw_line(uint8_t *buffer, int x0, int y0, int x1, int y1)
{
  int dx = abs(x1 - x0), dy = abs(y1 - y0);
  int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
  int err = dx - dy;
  while (1)
  {
    set_pixel(buffer, x0, y0);
    if (x0 == x1 && y0 == y1)
      break;
    int e2 = 2 * err;
    if (e2 > -dy)
    {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx)
    {
      err += dx;
      y0 += sy;
    }
  }
}

void set_display_rotation(esp_lcd_panel_io_handle_t io_handle, bool flipped)
{
  uint8_t seg = flipped ? 0xA1 : 0xA0;
  uint8_t com = flipped ? 0xC8 : 0xC0;
  esp_lcd_panel_io_tx_param(io_handle, seg, NULL, 0);
  esp_lcd_panel_io_tx_param(io_handle, com, NULL, 0);
}

// Состояние 1: текстовая информация
static void state_demo_text(esp_lcd_panel_handle_t panel, uint8_t *buf,
                            int *counter)
{
  ESP_LOGI(TAG, "State 1: text");
  char tmp[20];
  clear_screen(buf);
  draw_string(buf, "ESP-IDF v5.5", 0, 0);
  draw_string(buf, "SSD1306 Demo", 0, 10);
  snprintf(tmp, sizeof(tmp), "Counter: %d", (*counter)++);
  draw_string(buf, tmp, 0, 20);
  draw_string(buf, "Hello World!", 0, 40);
  draw_string(buf, "OLED Display", 0, 50);
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0,
                                            SCREEN_WIDTH, SCREEN_HEIGHT, buf));
  vTaskDelay(pdMS_TO_TICKS(STATE_DURATION_MS));
}

// Состояние 2: графика + текст
static void state_demo_graphics(esp_lcd_panel_handle_t panel, uint8_t *buf)
{
  ESP_LOGI(TAG, "State 2: graphics");
  clear_screen(buf);
  draw_string(buf, "Graphics Test", 0, 0);

  // Прямоугольная рамка
  for (int x = 10; x < SCREEN_WIDTH - 10; x++)
  {
    buf[x + (10 / 8) * SCREEN_WIDTH] |= (1 << (10 % 8));
    buf[x + ((SCREEN_HEIGHT - 10) / 8) * SCREEN_WIDTH] |=
        (1 << ((SCREEN_HEIGHT - 10) % 8));
  }
  for (int y = 10; y < SCREEN_HEIGHT - 10; y++)
  {
    buf[10 + (y / 8) * SCREEN_WIDTH] |= (1 << (y % 8));
    buf[SCREEN_WIDTH - 11 + (y / 8) * SCREEN_WIDTH] |= (1 << (y % 8));
  }

  // Диагональные линии
  draw_line(buf, 15, 15, SCREEN_WIDTH - 15, SCREEN_HEIGHT - 15);
  draw_line(buf, SCREEN_WIDTH - 15, 15, 15, SCREEN_HEIGHT - 15);

  draw_string(buf, "Lines & Frame", 30, 25);
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0,
                                            SCREEN_WIDTH, SCREEN_HEIGHT, buf));
  vTaskDelay(pdMS_TO_TICKS(STATE_DURATION_MS));
}

// Состояние 3: анимация столбчатой диаграммы
static void state_demo_animation(esp_lcd_panel_handle_t panel, uint8_t *buf)
{
  ESP_LOGI(TAG, "State 3: animation");
  char tmp[20];
  clear_screen(buf);
  draw_string(buf, "Animation", 40, 0);

  for (int i = 0; i < 8; i++)
  {
    int x = 20 + i * 12;
    int height = 10 + (i * 3);
    for (int y = SCREEN_HEIGHT - 10; y > SCREEN_HEIGHT - 10 - height; y--)
      for (int bw = 0; bw < 8; bw++)
        buf[(x + bw) + (y / 8) * SCREEN_WIDTH] |= (1 << (y % 8));

    clear_rect(buf, 0, 56, SCREEN_WIDTH, 8);
    snprintf(tmp, sizeof(tmp), "Frame: %d", i + 1);
    draw_string(buf, tmp, 20, 56);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0,
                                              SCREEN_WIDTH, SCREEN_HEIGHT, buf));
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  vTaskDelay(pdMS_TO_TICKS(1500));
}

// Состояние 4: пустой экран (ничего не отображается)
static void state_blank(esp_lcd_panel_handle_t panel, uint8_t *buf)
{
  ESP_LOGI(TAG, "State 4: blank screen");
  clear_screen(buf);
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0,
                                            SCREEN_WIDTH, SCREEN_HEIGHT, buf));
  vTaskDelay(pdMS_TO_TICKS(BLANK_DURATION_MS));
}

void app_main(void)
{
  ESP_LOGI(TAG, "Initializing I2C");

  i2c_config_t i2c_bus_config = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = I2C_MASTER_SDA_IO,
      .sda_pullup_en = GPIO_PULLUP_DISABLE,
      .scl_io_num = I2C_MASTER_SCL_IO,
      .scl_pullup_en = GPIO_PULLUP_DISABLE,
      .master.clk_speed = I2C_MASTER_FREQ_HZ,
  };
  i2c_bus_handle_t i2c_bus = i2c_bus_create(I2C_MASTER_NUM, &i2c_bus_config);
  if (i2c_bus == NULL)
  {
    ESP_LOGE(TAG, "Failed to create I2C bus");
    return;
  }

  esp_lcd_panel_io_i2c_config_t io_config = {
      .dev_addr = SSD1306_I2C_ADDRESS,
      .scl_speed_hz = I2C_MASTER_FREQ_HZ,
      .control_phase_bytes = 1,
      .dc_bit_offset = 6,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .on_color_trans_done = NULL,
      .user_ctx = NULL,
      .flags = {.dc_low_on_data = 0, .disable_control_phase = 0}};
  esp_lcd_panel_io_handle_t io_handle = NULL;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(
      i2c_bus_get_internal_bus_handle(i2c_bus), &io_config, &io_handle));

  esp_lcd_panel_handle_t panel_handle = NULL;
  esp_lcd_panel_dev_config_t panel_config = {
      .bits_per_pixel = 1,
      .reset_gpio_num = -1,
  };
  esp_lcd_panel_ssd1306_config_t ssd1306_config = {.height = SCREEN_HEIGHT};
  panel_config.vendor_config = &ssd1306_config;
  ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(
      io_handle, &panel_config, &panel_handle));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
  set_display_rotation(io_handle, true);

  uint8_t *buffer = heap_caps_malloc(
      SCREEN_WIDTH * SCREEN_HEIGHT / 8, MALLOC_CAP_DMA);
  if (!buffer)
  {
    ESP_LOGE(TAG, "Failed to allocate frame buffer!");
    return;
  }

  ESP_LOGI(TAG, "Display initialized, starting state machine");

  display_state_t state = STATE_DEMO_TEXT;
  int counter = 0;

  while (1)
  {
    switch (state)
    {
    case STATE_DEMO_TEXT:
      state_demo_text(panel_handle, buffer, &counter);
      break;
    case STATE_DEMO_GRAPHICS:
      state_demo_graphics(panel_handle, buffer);
      break;
    case STATE_DEMO_ANIMATION:
      state_demo_animation(panel_handle, buffer);
      break;
    case STATE_BLANK:
      state_blank(panel_handle, buffer);
      break;
    default:
      break;
    }
    // Переход к следующему состоянию по кругу
    state = (display_state_t)((state + 1) % STATE_COUNT);
  }

  free(buffer);
  esp_lcd_panel_del(panel_handle);
  esp_lcd_panel_io_del(io_handle);
  i2c_bus_delete(&i2c_bus);
}