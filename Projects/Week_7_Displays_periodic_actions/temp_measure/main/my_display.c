#include "my_display.h"

// Универсальная установка пикселя
static inline void set_pixel(uint8_t *buffer, int x, int y) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
        return;
    buffer[x + (y / 8) * SCREEN_WIDTH] |= (1 << (y % 8));
}

// Функция для вывода символа
void draw_char(uint8_t *buffer, char c, int x, int y) {
    if (c < 32 || c > 126) return;

    const uint8_t *char_data = font_5x8[c - 32];

    for (int col = 0; col < 5; col++) {
        uint8_t col_data = char_data[col];
        for (int row = 0; row < 8; row++) {
            if (col_data & (1 << row)) {
                int pixel_x = x + col;
                int pixel_y = y + row;
                if (pixel_x < SCREEN_WIDTH && pixel_y < SCREEN_HEIGHT) {
                    buffer[pixel_x + (pixel_y / 8) * SCREEN_WIDTH] |= (1 << (pixel_y % 8));
                }
            }
        }
    }
}

// Функция для вывода строки
void draw_string(uint8_t *buffer, const char *str, int x, int y) {
    int current_x = x;
    while (*str) {
        draw_char(buffer, *str, current_x, y);
        current_x += 6; // 5 пикселей символ + 1 пиксель пробел
        str++;
        if (current_x >= SCREEN_WIDTH - 5) break;
    }
}

// Функция очистки экрана
void clear_screen(uint8_t *buffer) {
    memset(buffer, 0x00, SCREEN_WIDTH * SCREEN_HEIGHT / 8);
}

// Функция очистки области экрана
void clear_rect(uint8_t *buffer, int x, int y, int width, int height) {
    for (int row = y; row < y + height; row++) {
        for (int col = x; col < x + width; col++) {
            if (col >= 0 && col < SCREEN_WIDTH && row >= 0 && row < SCREEN_HEIGHT) {
                buffer[col + (row / 8) * SCREEN_WIDTH] &= ~(1 << (row % 8));
            }
        }
    }
}

// Функция для рисования линии (алгоритм Брезенхема)
void draw_line(uint8_t *buffer, int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        set_pixel(buffer, x0, y0);
        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// Функция для переворота дисплея на 180 градусов
void set_display_rotation(esp_lcd_panel_io_handle_t io_handle, bool flipped) {
    if (flipped) {
        esp_lcd_panel_io_tx_param(io_handle, 0xA1, NULL, 0); // сегменты справа налево
        esp_lcd_panel_io_tx_param(io_handle, 0xC8, NULL, 0); // строки снизу вверх
    } else {
        esp_lcd_panel_io_tx_param(io_handle, 0xA0, NULL, 0); // сегменты слева направо
        esp_lcd_panel_io_tx_param(io_handle, 0xC0, NULL, 0); // строки сверху вниз
    }
}
