#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_err.h"

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_GPIO GPIO_NUM_13
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_14_BIT

static void help_print(void)
{
    printf("\nControl PWM from Serial Monitor (Wokwi)\n");
    printf("  + / - : change brightness\n");
    printf("  < / > : change frequency\n");
    printf("  i     : info\n");
    printf("  d     : raw duty\n");
    printf("  f     : get freq\n");
    printf("  h     : help\n\n");
}

void app_main(void)
{
    const uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT};
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_cfg));
    // Важно: RX/TX пины по умолчанию ок; драйвер для чтения поставим:
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));

    // PWM timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // PWM channel
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LEDC_GPIO,
        .duty = 0,
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    float brightness = 0.5f;
    const float step = 0.05f;

    const uint32_t freq[] = {5, 10, 25, 30, 35, 40, 50, 75, 100, 250, 500, 1000, 4000};
    const int f_len = (int)(sizeof(freq) / sizeof(freq[0]));
    int f_ind = f_len - 1; // старт с 4000

    const uint32_t DUTY_MAX = (1UL << LEDC_DUTY_RES) - 1;

    help_print();
    printf("Start: Frequency=%lu Hz, Brightness=%.2f\n", (unsigned long)freq[f_ind], brightness);

    uint32_t duty = (uint32_t)(DUTY_MAX * brightness);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

    while (1)
    {
        uint8_t c;
        int n = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(50));
        if (n == 1)
        {
            switch ((char)c)
            {
            case '+':
                brightness += step;
                break;
            case '-':
                brightness -= step;
                break;
            case '<':
                f_ind--;
                break;
            case '>':
                f_ind++;
                break;

            case 'i':
            case 'I':
                printf("Frequency=%lu Hz, Brightness=%.2f\n",
                       (unsigned long)freq[f_ind], brightness);
                break;

            case 'h':
            case 'H':
                help_print();
                break;

            case 'd':
            case 'D':
                printf("LEDC Duty=%lu\n", (unsigned long)ledc_get_duty(LEDC_MODE, LEDC_CHANNEL));
                break;

            case 'f':
            case 'F':
                printf("LEDC Frequency (selected)=%lu\n", (unsigned long)freq[f_ind]);
                break;

            default:
                break;
            }

            if (brightness > 1.0f)
                brightness = 1.0f;
            if (brightness < 0.0f)
                brightness = 0.0f;
            if (f_ind >= f_len)
                f_ind = 0;
            if (f_ind < 0)
                f_ind = f_len - 1;

            if (c == '+' || c == '-')
            {
                duty = (uint32_t)(DUTY_MAX * brightness);
                ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
                ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
                printf("Brightness=%.2f\n", brightness);
            }
            else if (c == '<' || c == '>')
            {
                ESP_ERROR_CHECK(ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq[f_ind]));
                printf("Frequency=%lu Hz\n", (unsigned long)freq[f_ind]);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
