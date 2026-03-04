/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "modbus_rtu";

static const gpio_num_t PIN_R = GPIO_NUM_33;
static const gpio_num_t PIN_G = GPIO_NUM_25;
static const gpio_num_t PIN_B = GPIO_NUM_26;

static const gpio_num_t PIN_RS485_DE_RE = GPIO_NUM_4;
static const gpio_num_t PIN_RS485_TX = GPIO_NUM_17;
static const gpio_num_t PIN_RS485_RX = GPIO_NUM_16;

static const uart_port_t UART_CONSOLE = UART_NUM_0;
static const uart_port_t UART_RS485 = UART_NUM_2;

static const uint8_t MB_SLAVE_ADDR = 1;
static const uint32_t MB_BAUD = 9600;

enum
{
    HOLDING_START = 0x0000,
    HOLDING_COUNT = 3,
    PWM_BITS = 13,
};

static uint16_t holding[HOLDING_COUNT] = {0};
static const uint32_t PWM_MAX = (1u << PWM_BITS) - 1u;
static const uint32_t PWM_FREQ = 4000;

static const ledc_mode_t PWM_MODE = LEDC_LOW_SPEED_MODE;
static const ledc_timer_t PWM_TIMER = LEDC_TIMER_0;
static const ledc_channel_t CH_R = LEDC_CHANNEL_0;
static const ledc_channel_t CH_G = LEDC_CHANNEL_1;
static const ledc_channel_t CH_B = LEDC_CHANNEL_2;

// Which UART the current Modbus frame arrived on — responses go back to the same port.
static uart_port_t g_response_uart = UART_NUM_2;

static uint16_t mb_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static void rs485_set_tx(bool tx)
{
    gpio_set_level(PIN_RS485_DE_RE, tx ? 1 : 0);
}

static void print_hex_line(const char *prefix, const uint8_t *buf, size_t n)
{
    printf("%s", prefix);
    for (size_t i = 0; i < n; i++)
    {
        if (i)
            putchar(' ');
        printf("%02X", buf[i]);
    }
    printf("\r\n");
    fflush(stdout);
}

static void apply_pwm_from_holding(void)
{
    for (int i = 0; i < (int)HOLDING_COUNT; i++)
        holding[i] &= (uint16_t)PWM_MAX;

    ledc_set_duty(PWM_MODE, CH_R, holding[0]);
    ledc_update_duty(PWM_MODE, CH_R);
    ledc_set_duty(PWM_MODE, CH_G, holding[1]);
    ledc_update_duty(PWM_MODE, CH_G);
    ledc_set_duty(PWM_MODE, CH_B, holding[2]);
    ledc_update_duty(PWM_MODE, CH_B);
}

static void send_modbus_response(const uint8_t *frame, size_t frame_len)
{
    if (g_response_uart == UART_RS485)
    {
        rs485_set_tx(true);
        uart_write_bytes(UART_RS485, (const char *)frame, frame_len);
        uart_wait_tx_done(UART_RS485, pdMS_TO_TICKS(50));
        rs485_set_tx(false);
    }
    else
    {
        uart_write_bytes(UART_CONSOLE, (const char *)frame, frame_len);
        uart_wait_tx_done(UART_CONSOLE, pdMS_TO_TICKS(50));
    }

    print_hex_line("TX: ", frame, frame_len);
}

static void send_exception(uint8_t addr, uint8_t func, uint8_t ex_code)
{
    uint8_t resp[5];
    resp[0] = addr;
    resp[1] = func | 0x80;
    resp[2] = ex_code;
    uint16_t crc = mb_crc16(resp, 3);
    resp[3] = (uint8_t)(crc & 0xFF);
    resp[4] = (uint8_t)((crc >> 8) & 0xFF);
    send_modbus_response(resp, sizeof(resp));
}

static bool addr_in_range(uint16_t reg_addr, uint16_t qty)
{
    if (qty == 0)
        return false;
    uint32_t start = reg_addr;
    uint32_t end = (uint32_t)reg_addr + (uint32_t)qty - 1u;

    uint32_t lo = HOLDING_START;
    uint32_t hi = (uint32_t)HOLDING_START + (uint32_t)HOLDING_COUNT - 1u;

    return (start >= lo) && (end <= hi);
}

static void handle_modbus_frame(const uint8_t *frame, size_t len)
{
    if (len < 4)
        return;

    uint16_t crc_rx = (uint16_t)frame[len - 2] | ((uint16_t)frame[len - 1] << 8);
    uint16_t crc_ok = mb_crc16(frame, len - 2);
    if (crc_rx != crc_ok)
    {
        ESP_LOGW(TAG, "CRC mismatch (rx=%04X calc=%04X)", crc_rx, crc_ok);
        return;
    }

    uint8_t addr = frame[0];
    uint8_t func = frame[1];

    if (!(addr == MB_SLAVE_ADDR || addr == 0))
        return;
    bool is_broadcast = (addr == 0);

    if (func == 0x03)
    {
        if (len != 8)
        {
            if (!is_broadcast)
                send_exception(MB_SLAVE_ADDR, func, 0x03);
            return;
        }

        uint16_t start = (uint16_t)frame[2] << 8 | frame[3];
        uint16_t qty = (uint16_t)frame[4] << 8 | frame[5];

        if (!addr_in_range(start, qty))
        {
            if (!is_broadcast)
                send_exception(MB_SLAVE_ADDR, func, 0x02);
            return;
        }
        if (qty > 125)
        {
            if (!is_broadcast)
                send_exception(MB_SLAVE_ADDR, func, 0x03);
            return;
        }

        const size_t resp_len = 3 + (size_t)qty * 2 + 2;
        uint8_t resp[3 + 2 * 125 + 2];

        resp[0] = MB_SLAVE_ADDR;
        resp[1] = 0x03;
        resp[2] = (uint8_t)(qty * 2);

        for (uint16_t i = 0; i < qty; i++)
        {
            uint16_t reg = holding[(start - HOLDING_START) + i];
            resp[3 + 2 * i] = (uint8_t)(reg >> 8);
            resp[3 + 2 * i + 1] = (uint8_t)(reg & 0xFF);
        }

        uint16_t crc = mb_crc16(resp, resp_len - 2);
        resp[resp_len - 2] = (uint8_t)(crc & 0xFF);
        resp[resp_len - 1] = (uint8_t)(crc >> 8);

        if (!is_broadcast)
            send_modbus_response(resp, resp_len);
        return;
    }

    if (func == 0x06)
    {
        if (len != 8)
        {
            if (!is_broadcast)
                send_exception(MB_SLAVE_ADDR, func, 0x03);
            return;
        }

        uint16_t reg_addr = (uint16_t)frame[2] << 8 | frame[3];
        uint16_t value = (uint16_t)frame[4] << 8 | frame[5];

        if (!addr_in_range(reg_addr, 1))
        {
            if (!is_broadcast)
                send_exception(MB_SLAVE_ADDR, func, 0x02);
            return;
        }

        holding[reg_addr - HOLDING_START] = (uint16_t)(value & PWM_MAX);
        apply_pwm_from_holding();

        if (!is_broadcast)
            send_modbus_response(frame, len);
        return;
    }

    if (func == 0x10)
    {
        if (len < 9)
        {
            if (!is_broadcast)
                send_exception(MB_SLAVE_ADDR, func, 0x03);
            return;
        }

        uint16_t start = (uint16_t)frame[2] << 8 | frame[3];
        uint16_t qty = (uint16_t)frame[4] << 8 | frame[5];
        uint8_t bc = frame[6];

        if (qty == 0 || qty > 123)
        {
            if (!is_broadcast)
                send_exception(MB_SLAVE_ADDR, func, 0x03);
            return;
        }
        if (bc != (uint8_t)(qty * 2))
        {
            if (!is_broadcast)
                send_exception(MB_SLAVE_ADDR, func, 0x03);
            return;
        }

        size_t expected_len = 7 + (size_t)bc + 2;
        if (len != expected_len)
        {
            if (!is_broadcast)
                send_exception(MB_SLAVE_ADDR, func, 0x03);
            return;
        }

        if (!addr_in_range(start, qty))
        {
            if (!is_broadcast)
                send_exception(MB_SLAVE_ADDR, func, 0x02);
            return;
        }

        size_t off = 7;
        for (uint16_t i = 0; i < qty; i++)
        {
            uint16_t v = ((uint16_t)frame[off] << 8) | frame[off + 1];
            holding[(start - HOLDING_START) + i] = (uint16_t)(v & PWM_MAX);
            off += 2;
        }
        apply_pwm_from_holding();

        if (!is_broadcast)
        {
            uint8_t resp[8];
            resp[0] = MB_SLAVE_ADDR;
            resp[1] = 0x10;
            resp[2] = frame[2];
            resp[3] = frame[3];
            resp[4] = frame[4];
            resp[5] = frame[5];
            uint16_t crc = mb_crc16(resp, 6);
            resp[6] = (uint8_t)(crc & 0xFF);
            resp[7] = (uint8_t)(crc >> 8);
            send_modbus_response(resp, sizeof(resp));
        }
        return;
    }

    if (!is_broadcast)
        send_exception(MB_SLAVE_ADDR, func, 0x01);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

static bool parse_hex_line_to_bytes(const char *line, uint8_t *out, size_t *out_len, size_t max_len)
{
    *out_len = 0;
    size_t i = 0;
    size_t n = strlen(line);

    while (i < n)
    {
        while (i < n && isspace((unsigned char)line[i]))
            i++;
        if (i >= n)
            break;

        if (i + 1 >= n)
            return false;
        int h1 = hexval(line[i]);
        int h2 = hexval(line[i + 1]);
        if (h1 < 0 || h2 < 0)
            return false;

        if (*out_len >= max_len)
            return false;
        out[(*out_len)++] = (uint8_t)((h1 << 4) | h2);

        i += 2;
        while (i < n && (line[i] == ' ' || line[i] == '\t' || line[i] == ','))
            i++;
    }

    return (*out_len >= 2);
}

static void process_console_line(const char *line)
{
    while (*line && isspace((unsigned char)*line))
        line++;
    if (*line == '\0')
        return;

    uint8_t req[260];
    size_t n = 0;

    if (!parse_hex_line_to_bytes(line, req, &n, sizeof(req) - 2))
    {
        printf("ERR: cannot parse hex line\r\n");
        printf("Example: 01 06 00 00 1F FF\r\n");
        return;
    }

    bool crc_ok = false;
    if (n >= 4)
    {
        uint16_t crc_rx = (uint16_t)req[n - 2] | ((uint16_t)req[n - 1] << 8);
        uint16_t crc = mb_crc16(req, n - 2);
        crc_ok = (crc == crc_rx);
    }
    if (!crc_ok)
    {
        uint16_t crc = mb_crc16(req, n);
        req[n++] = (uint8_t)(crc & 0xFF);
        req[n++] = (uint8_t)(crc >> 8);
    }

    print_hex_line("RX: ", req, n);
    g_response_uart = UART_CONSOLE;
    handle_modbus_frame(req, n);
}

static void process_console_binary_frame(uint8_t *frame, size_t frame_len)
{
    if (frame_len < 2)
        return;

    size_t n = frame_len;
    bool crc_ok = false;
    if (n >= 4)
    {
        uint16_t crc_rx = (uint16_t)frame[n - 2] | ((uint16_t)frame[n - 1] << 8);
        uint16_t crc = mb_crc16(frame, n - 2);
        crc_ok = (crc == crc_rx);
    }

    if (!crc_ok)
    {
        if (n + 2 > 260)
            return;
        uint16_t crc = mb_crc16(frame, n);
        frame[n++] = (uint8_t)(crc & 0xFF);
        frame[n++] = (uint8_t)(crc >> 8);
    }

    print_hex_line("RX(bin): ", frame, n);
    g_response_uart = UART_CONSOLE;
    handle_modbus_frame(frame, n);
}

static void console_rx_task(void *arg)
{
    (void)arg;
    char line[320];
    size_t line_len = 0;

    uint8_t frame[260];
    size_t frame_len = 0;
    size_t expected_len = 0;
    int64_t last_byte_us = 0;
    const int64_t frame_idle_us = 100000; // TCP/RFC2217 can have big inter-byte gaps

    while (1)
    {
        uint8_t c;
        int r = uart_read_bytes(UART_CONSOLE, &c, 1, pdMS_TO_TICKS(20));
        if (r > 0)
        {
            if (c == '\r')
                goto maybe_flush;

            if (c == '\n')
            {
                line[line_len] = '\0';
                process_console_line(line);
                line_len = 0;
                goto maybe_flush;
            }

            // If the byte is non-printable (or we are already inside a binary frame),
            // treat it as a raw Modbus RTU byte. Once binary collection starts ALL
            // subsequent bytes go into the frame so printable CRC bytes (e.g. 0x7A 'z')
            // are not accidentally routed to the ASCII line buffer.
            bool in_binary = (frame_len > 0);
            bool non_print = !(isprint((unsigned char)c) || c == ' ' || c == '\t' || c == ',');

            if (!in_binary && !non_print)
            {
                if (line_len + 1 < sizeof(line))
                    line[line_len++] = (char)c;
            }
            else
            {
                if (frame_len == 0)
                    line_len = 0; // drop any partially typed ASCII line

                if (frame_len < sizeof(frame))
                {
                    frame[frame_len++] = c;
                    last_byte_us = esp_timer_get_time();

                    // Determine expected Modbus RTU request length once we have enough bytes
                    // addr(1) + func(1) + ... + crc(2)
                    if (frame_len >= 2 && expected_len == 0)
                    {
                        switch (frame[1])
                        {
                        case 0x03:
                        case 0x06:
                            expected_len = 8;
                            break;
                        case 0x10:
                            // need byte-count at offset 6, so wait until we have header
                            break;
                        default:
                            // unknown: will flush on idle timeout
                            break;
                        }
                    }
                    if (frame_len >= 7 && expected_len == 0 && frame[1] == 0x10)
                    {
                        expected_len = 7u + (size_t)frame[6] + 2u;
                    }

                    if (expected_len > 0 && frame_len >= expected_len)
                    {
                        process_console_binary_frame(frame, expected_len);
                        frame_len = 0;
                        expected_len = 0;
                        last_byte_us = 0;
                    }
                }
            }
        }

    maybe_flush:
        if (frame_len > 0)
        {
            int64_t now = esp_timer_get_time();
            if (last_byte_us != 0 && (now - last_byte_us) > frame_idle_us)
            {
                process_console_binary_frame(frame, frame_len);
                frame_len = 0;
                expected_len = 0;
                last_byte_us = 0;
            }
        }
    }
}

static void rs485_rx_task(void *arg)
{
    (void)arg;

    uint8_t frame[260];
    size_t frame_len = 0;
    int64_t last_byte_us = 0;
    const int64_t frame_gap_us = 6000; // ~3.5 chars @ 9600 (safe margin)

    while (1)
    {
        uint8_t b[64];
        int r = uart_read_bytes(UART_RS485, b, sizeof(b), pdMS_TO_TICKS(20));
        if (r > 0)
        {
            int copy = r;
            if (frame_len + (size_t)copy > sizeof(frame))
                copy = (int)(sizeof(frame) - frame_len);
            if (copy > 0)
            {
                memcpy(&frame[frame_len], b, (size_t)copy);
                frame_len += (size_t)copy;
                last_byte_us = esp_timer_get_time();
            }
        }

        if (frame_len > 0)
        {
            int64_t now = esp_timer_get_time();
            if (last_byte_us != 0 && (now - last_byte_us) > frame_gap_us)
            {
                print_hex_line("RS485 RX: ", frame, frame_len);
                g_response_uart = UART_RS485;
                handle_modbus_frame(frame, frame_len);
                frame_len = 0;
                last_byte_us = 0;
            }
        }
    }
}

static void init_pwm(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = PWM_MODE,
        .duty_resolution = (ledc_timer_bit_t)PWM_BITS,
        .timer_num = PWM_TIMER,
        .freq_hz = PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const ledc_channel_config_t ch_r = {
        .gpio_num = PIN_R,
        .speed_mode = PWM_MODE,
        .channel = CH_R,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    const ledc_channel_config_t ch_g = {
        .gpio_num = PIN_G,
        .speed_mode = PWM_MODE,
        .channel = CH_G,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    const ledc_channel_config_t ch_b = {
        .gpio_num = PIN_B,
        .speed_mode = PWM_MODE,
        .channel = CH_B,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    ESP_ERROR_CHECK(ledc_channel_config(&ch_r));
    ESP_ERROR_CHECK(ledc_channel_config(&ch_g));
    ESP_ERROR_CHECK(ledc_channel_config(&ch_b));
}

static void init_rs485_uart(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_RS485_DE_RE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    rs485_set_tx(false);

    uart_config_t cfg = {
        .baud_rate = (int)MB_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(UART_RS485, 1024, 1024, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(uart_param_config(UART_RS485, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_RS485, PIN_RS485_TX, PIN_RS485_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void init_console_uart_rx(void)
{
    // UART0 driver is typically already installed for the console.
    // Still, try to install to ensure uart_read_bytes works; ignore "already installed".
    esp_err_t err = uart_driver_install(UART_CONSOLE, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        ESP_ERROR_CHECK(err);
}

static void tick_task(void *arg)
{
    (void)arg;
    uint32_t tickN = 0;
    while (1)
    {
        printf("tick %" PRIu32 " | holding: %u, %u, %u\r\n", tickN++, holding[0], holding[1], holding[2]);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    printf("\r\n=== BOOT ===\r\n");
    printf("Type Modbus frame bytes in HEX, without CRC. Example:\r\n");
    printf("  01 06 00 00 1F FF\r\n");
    printf("(CRC will be appended automatically if missing/wrong)\r\n\r\n");

    init_pwm();
    apply_pwm_from_holding();

    init_console_uart_rx();
    init_rs485_uart();

    printf("Modbus slave: addr=%u, holding %u..%u => R,G,B (0..%" PRIu32 ")\r\n",
           MB_SLAVE_ADDR,
           HOLDING_START,
           (uint16_t)(HOLDING_START + HOLDING_COUNT - 1),
           PWM_MAX);
    fflush(stdout);

    xTaskCreate(console_rx_task, "console_rx", 4096, NULL, 10, NULL);
    xTaskCreate(rs485_rx_task, "rs485_rx", 4096, NULL, 9, NULL);
    xTaskCreate(tick_task, "tick", 2048, NULL, 1, NULL);
}
