#include "my_display.h"

#include <stdlib.h>

static inline void set_pixel(uint8_t *buffer, int x, int y)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }
    buffer[x + (y / 8) * SCREEN_WIDTH] |= (1 << (y % 8));
}

void draw_char(uint8_t *buffer, char c, int x, int y)
{
    if (c < 32 || c > 126) {
        return;
    }

    const uint8_t *char_data = font_5x8[c - 32];

    for (int col = 0; col < 5; col++) {
        uint8_t col_data = char_data[col];
        for (int row = 0; row < 8; row++) {
            if (col_data & (1 << row)) {
                int px = x + col;
                int py = y + row;
                if (px < SCREEN_WIDTH && py < SCREEN_HEIGHT) {
                    buffer[px + (py / 8) * SCREEN_WIDTH] |= (1 << (py % 8));
                }
            }
        }
    }
}

void draw_string(uint8_t *buffer, const char *str, int x, int y)
{
    int cx = x;
    while (*str) {
        draw_char(buffer, *str, cx, y);
        cx += 6;
        str++;
        if (cx >= SCREEN_WIDTH - 5) {
            break;
        }
    }
}

void clear_screen(uint8_t *buffer)
{
    memset(buffer, 0x00, SCREEN_WIDTH * SCREEN_HEIGHT / 8);
}

void clear_rect(uint8_t *buffer, int x, int y, int width, int height)
{
    for (int row = y; row < y + height; row++) {
        for (int col = x; col < x + width; col++) {
            if (col >= 0 && col < SCREEN_WIDTH && row >= 0 && row < SCREEN_HEIGHT) {
                buffer[col + (row / 8) * SCREEN_WIDTH] &= ~(1 << (row % 8));
            }
        }
    }
}

void draw_line(uint8_t *buffer, int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        set_pixel(buffer, x0, y0);
        if (x0 == x1 && y0 == y1) {
            break;
        }

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

void set_display_rotation(esp_lcd_panel_io_handle_t io_handle, bool flipped)
{
    uint8_t seg = flipped ? 0xA1 : 0xA0;
    uint8_t com = flipped ? 0xC8 : 0xC0;
    esp_lcd_panel_io_tx_param(io_handle, seg, NULL, 0);
    esp_lcd_panel_io_tx_param(io_handle, com, NULL, 0);
}

