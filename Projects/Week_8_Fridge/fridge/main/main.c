/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "i2c_bus.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

#include "my_display.h"
#include "dht.h"

static const char *TAG = "FRIDGE";

// ─────────────────────────────────────────────────────────────
// Подключение в Wokwi
// ─────────────────────────────────────────────────────────────
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO GPIO_NUM_21
#define I2C_MASTER_SCL_IO GPIO_NUM_22
#define I2C_MASTER_FREQ_HZ 100000

#define OLED_I2C_ADDRESS 0x3C

#define BTN_TEMPERATURE_GPIO GPIO_NUM_26
#define BTN_INFO_GPIO GPIO_NUM_14
#define BTN_DOOR_GPIO GPIO_NUM_27

#define LED_PWM_GPIO GPIO_NUM_15

// DHT22 (как в проекте недели 7: temp_measure)
#define DHT22_GPIO GPIO_NUM_4

// ─────────────────────────────────────────────────────────────
// Тайминги / константы
// ─────────────────────────────────────────────────────────────
#define DEBOUNCE_MS 60
#define CLICK_COOLDOWN_MS 220
#define INFO_TIMEOUT_MS 5000
#define DOOR_ALERT_MS 30000

static const double DT = 0.5;
static const int TIMER_PERIOD_US = (int)(0.5 * 1000000);

static const double P_NOM_W = 70.0;

// Настройка тепловой модели (это не физическая калибровка)
static double C_th = 300.0;
static double R_th = 2.0;
static double k_door = 20.0;

// Настройка ПИД
static double kp = 0.8;
static double ki = 0.08;
static double kd = 0.04;
static double integrator = 0.0;
static double prev_error = 0.0;
static const double integrator_min = -10.0;
static const double integrator_max = 10.0;

typedef struct {
    uint32_t gpio_num;
} button_event_t;

static QueueHandle_t s_button_queue = NULL;

typedef struct {
    // состояние модели
    double t_in_c;
    double t_ambient_c;
    double setpoint_c;
    double u; // 0..1
    bool door_open;

    // состояние UI
    uint8_t info_page;             // 0: T_in (внутри), 1: T_ambient (снаружи)
    int64_t info_deadline_us;      // когда очищать дисплей
} fridge_state_t;

static fridge_state_t s_state = {
    .t_in_c = 25.0,
    .t_ambient_c = 25.0,
    .setpoint_c = 5.0,
    .u = 0.0,
    .door_open = false,
    .info_page = 0,
    .info_deadline_us = 0,
};

static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;

static inline double clamp(double x, double lo, double hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline int64_t now_us(void)
{
    return esp_timer_get_time();
}

static void log_event(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    double t_s = (double)now_us() / 1000000.0;
    printf("[%.3f] ", t_s);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static double pid_compute_cooling(double setpoint, double measurement, double dt)
{
    // Для охлаждения: положительная ошибка значит "слишком тепло" -> увеличиваем мощность охлаждения.
    double error = measurement - setpoint;

    integrator = clamp(integrator + error * dt, integrator_min, integrator_max);
    double derivative = (error - prev_error) / dt;
    prev_error = error;

    return clamp(kp * error + ki * integrator + kd * derivative, 0.0, 1.0);
}

// ─────────────────────────────────────────────────────────────
// ISR: отправка событий кнопок в очередь
// ─────────────────────────────────────────────────────────────
static void IRAM_ATTR button_isr_handler(void *arg)
{
    button_event_t evt = {.gpio_num = (uint32_t)arg};
    BaseType_t hpw = pdFALSE;
    xQueueSendFromISR(s_button_queue, &evt, &hpw);
    portYIELD_FROM_ISR(hpw);
}

static void gpio_init_buttons(void)
{
    // Настраиваем входы с подтяжкой вверх
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BTN_TEMPERATURE_GPIO) |
                        (1ULL << BTN_INFO_GPIO) |
                        (1ULL << BTN_DOOR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    // Кнопки Temperature/Info/Door: только на нажатие (спад).
    ESP_ERROR_CHECK(gpio_set_intr_type(BTN_TEMPERATURE_GPIO, GPIO_INTR_NEGEDGE));
    ESP_ERROR_CHECK(gpio_set_intr_type(BTN_INFO_GPIO, GPIO_INTR_NEGEDGE));
    ESP_ERROR_CHECK(gpio_set_intr_type(BTN_DOOR_GPIO, GPIO_INTR_NEGEDGE));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BTN_TEMPERATURE_GPIO, button_isr_handler, (void *)BTN_TEMPERATURE_GPIO));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BTN_INFO_GPIO, button_isr_handler, (void *)BTN_INFO_GPIO));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BTN_DOOR_GPIO, button_isr_handler, (void *)BTN_DOOR_GPIO));
}

// ─────────────────────────────────────────────────────────────
// ШИМ для светодиода (LEDC)
// ─────────────────────────────────────────────────────────────
static void led_pwm_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));

    ledc_channel_config_t ccfg = {
        .gpio_num = LED_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
}

static void led_pwm_set_u(double u)
{
    u = clamp(u, 0.0, 1.0);
    uint32_t duty = (uint32_t)lround(u * ((1U << 10) - 1));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ─────────────────────────────────────────────────────────────
// Таймер контура управления: шаг модели и ПИД
// ─────────────────────────────────────────────────────────────
static void sim_step(void)
{
    portENTER_CRITICAL(&s_state_mux);

    double t_in = s_state.t_in_c;
    double t_amb = s_state.t_ambient_c;
    double setp = s_state.setpoint_c;
    bool door = s_state.door_open;

    double u = pid_compute_cooling(setp, t_in, DT);

    // Теплоприток из окружающей среды (положительный, если снаружи теплее, чем внутри)
    double w_env = (t_amb - t_in) / R_th;

    // Открытая дверь усиливает теплообмен с окружающей средой
    double w_door = door ? k_door * (t_amb - t_in) : 0.0;

    // Охлаждение отводит тепло -> температура стремится снижаться
    double w_cool = -P_NOM_W * u;

    double w_net = w_env + w_door + w_cool;
    t_in = t_in + (w_net / C_th) * DT;

    s_state.t_in_c = clamp(t_in, -10.0, 30.0);
    s_state.u = u;

    portEXIT_CRITICAL(&s_state_mux);
}

static void sim_timer_cb(void *arg)
{
    (void)arg;
    sim_step();
}

// ─────────────────────────────────────────────────────────────
// Задачи
// ─────────────────────────────────────────────────────────────
static void input_task(void *arg)
{
    (void)arg;

    const double setpoints[] = {1.0, 2.0, 5.0, 8.0};
    size_t sp_idx = 2; // стартуем с +5C

    TickType_t last_press_ticks[64] = {0};
    int64_t cooldown_until_us[64] = {0};

    button_event_t evt;
    while (1) {
        if (!xQueueReceive(s_button_queue, &evt, portMAX_DELAY)) {
            continue;
        }

        uint32_t gpio_num = evt.gpio_num;
        int64_t t_us = now_us();
        TickType_t now = xTaskGetTickCount();
        if ((now - last_press_ticks[gpio_num]) * portTICK_PERIOD_MS < DEBOUNCE_MS) {
            continue;
        }
        if (t_us < cooldown_until_us[gpio_num]) {
            continue;
        }

        // Принимаем только событие «нажато» (активный уровень — низкий).
        if (gpio_get_level((gpio_num_t)gpio_num) != 0) {
            continue;
        }

        last_press_ticks[gpio_num] = now;
        cooldown_until_us[gpio_num] = t_us + (int64_t)CLICK_COOLDOWN_MS * 1000;

        if (gpio_num == BTN_TEMPERATURE_GPIO) {
            sp_idx = (sp_idx + 1) % (sizeof(setpoints) / sizeof(setpoints[0]));
            portENTER_CRITICAL(&s_state_mux);
            s_state.setpoint_c = setpoints[sp_idx];
            integrator = 0.0;
            prev_error = 0.0;
            portEXIT_CRITICAL(&s_state_mux);
            log_event("Temperature setpoint -> +%.0f C", setpoints[sp_idx]);
        } else if (gpio_num == BTN_DOOR_GPIO) {
            bool door;
            portENTER_CRITICAL(&s_state_mux);
            s_state.door_open = !s_state.door_open;
            door = s_state.door_open;
            portEXIT_CRITICAL(&s_state_mux);
            (void)door;
        } else if (gpio_num == BTN_INFO_GPIO) {
            int64_t deadline = now_us() + (int64_t)INFO_TIMEOUT_MS * 1000;
            portENTER_CRITICAL(&s_state_mux);
            bool was_active = now_us() < s_state.info_deadline_us;
            if (!was_active) {
                s_state.info_page = 0;
            } else {
                s_state.info_page = (uint8_t)((s_state.info_page + 1) & 0x01);
            }
            s_state.info_deadline_us = deadline;
            portEXIT_CRITICAL(&s_state_mux);
        }

        // Ждём отпускания, чтобы не было повторов при удержании.
        while (gpio_get_level((gpio_num_t)gpio_num) == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

typedef struct {
    i2c_bus_handle_t i2c_bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
} display_ctx_t;

static void display_task(void *arg)
{
    display_ctx_t *ctx = (display_ctx_t *)arg;

    uint8_t *buf = heap_caps_malloc(SCREEN_WIDTH * SCREEN_HEIGHT / 8, MALLOC_CAP_DMA);
    if (!buf) {
        ESP_LOGE(TAG, "No memory for OLED buffer");
        vTaskDelete(NULL);
        return;
    }

    bool was_active = false;
    uint8_t last_page = 0xFF;
    int64_t last_draw_us = 0;

    while (1) {
        fridge_state_t snap;
        portENTER_CRITICAL(&s_state_mux);
        snap = s_state;
        portEXIT_CRITICAL(&s_state_mux);

        int64_t t = now_us();
        bool active = t < snap.info_deadline_us;

        if (!active) {
            if (was_active) {
                clear_screen(buf);
                ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(ctx->panel, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, buf));
            }
            was_active = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        bool need_redraw = (!was_active) || (snap.info_page != last_page) || (t - last_draw_us) > 250000;
        if (need_redraw) {
            clear_screen(buf);
            char line[24];
            if (snap.info_page == 0) {
                draw_string(buf, "Fridge inside", 0, 0);
                snprintf(line, sizeof(line), "T_in: %.2f C", snap.t_in_c);
                draw_string(buf, line, 0, 18);
                snprintf(line, sizeof(line), "Set:  %.0f C", snap.setpoint_c);
                draw_string(buf, line, 0, 36);
            } else {
                draw_string(buf, "Ambient", 0, 0);
                snprintf(line, sizeof(line), "T_out: %.2f C", snap.t_ambient_c);
                draw_string(buf, line, 0, 18);
                snprintf(line, sizeof(line), "Door: %s", snap.door_open ? "OPEN" : "CLOSED");
                draw_string(buf, line, 0, 36);
            }
            snprintf(line, sizeof(line), "P: %.0f%%", snap.u * 100.0);
            draw_string(buf, line, 0, 54);

            ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(ctx->panel, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, buf));
            last_draw_us = t;
            last_page = snap.info_page;
        }

        was_active = true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void sensor_task(void *arg)
{
    (void)arg;

    while (1) {
        float temp_c = 0.0f;
        float hum = 0.0f;
        if (dht_read_float_data(DHT_TYPE_AM2301, DHT22_GPIO, &hum, &temp_c) == ESP_OK) {
            portENTER_CRITICAL(&s_state_mux);
            s_state.t_ambient_c = temp_c;
            portEXIT_CRITICAL(&s_state_mux);
        } else {
            ESP_LOGW(TAG, "DHT22 read error");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void pwm_task(void *arg)
{
    (void)arg;
    while (1) {
        double u;
        portENTER_CRITICAL(&s_state_mux);
        u = s_state.u;
        portEXIT_CRITICAL(&s_state_mux);
        led_pwm_set_u(u);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void logger_task(void *arg)
{
    (void)arg;
    double last_p_w = -1.0;
    double last_t_logged = 1e9;
    bool last_door = false;
    int64_t door_open_since_us = 0;
    bool door_alert_fired = false;

    while (1) {
        fridge_state_t snap;
        portENTER_CRITICAL(&s_state_mux);
        snap = s_state;
        portEXIT_CRITICAL(&s_state_mux);

        double p_w = snap.u * P_NOM_W;
        if (last_p_w < 0) {
            last_p_w = p_w;
            last_t_logged = snap.t_in_c;
            last_door = snap.door_open;
            if (snap.door_open) {
                door_open_since_us = now_us();
            }
        }

        if (snap.door_open != last_door) {
            log_event("Door %s", snap.door_open ? "OPEN" : "CLOSED");
            last_door = snap.door_open;
            if (snap.door_open) {
                door_open_since_us = now_us();
                door_alert_fired = false;
            } else {
                door_open_since_us = 0;
                door_alert_fired = false;
            }
        }

        if (snap.door_open && !door_alert_fired && door_open_since_us > 0) {
            int64_t open_for_us = now_us() - door_open_since_us;
            if (open_for_us >= (int64_t)DOOR_ALERT_MS * 1000) {
                log_event("ALERT: door open for %d seconds", DOOR_ALERT_MS / 1000);
                door_alert_fired = true;
            }
        }

        if (fabs(p_w - last_p_w) >= 5.0) {
            log_event("Cooling power %.1f W (%.0f%%)", p_w, snap.u * 100.0);
            last_p_w = p_w;
        }

        if (fabs(snap.t_in_c - last_t_logged) >= 1.0) {
            log_event("T_in changed: %.2f C", snap.t_in_c);
            last_t_logged = snap.t_in_c;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ─────────────────────────────────────────────────────────────
// app_main (точка входа)
// ─────────────────────────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "Fridge start (PID + model + OLED + DHT22 + ISR buttons)");

    s_button_queue = xQueueCreate(16, sizeof(button_event_t));
    if (!s_button_queue) {
        ESP_LOGE(TAG, "Failed to create button queue");
        return;
    }

    gpio_init_buttons();
    led_pwm_init();

    // Создаём I2C-шину один раз (для OLED)
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_bus_handle_t i2c_bus = i2c_bus_create(I2C_MASTER_NUM, &i2c_conf);
    if (!i2c_bus) {
        ESP_LOGE(TAG, "Failed to create i2c bus");
        return;
    }

    // Инициализация OLED (SSD1306 по I2C через esp_lcd)
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = OLED_I2C_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .flags = {.dc_low_on_data = 0, .disable_control_phase = 0},
    };

    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(
        i2c_bus_get_internal_bus_handle(i2c_bus), &io_cfg, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
    };
    esp_lcd_panel_ssd1306_config_t ssd1306_cfg = {.height = SCREEN_HEIGHT};
    panel_cfg.vendor_config = &ssd1306_cfg;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_cfg, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    set_display_rotation(io_handle, true);

    // периодический таймер симуляции
    const esp_timer_create_args_t timer_args = {
        .callback = sim_timer_cb,
        .name = "fridge_sim",
    };
    esp_timer_handle_t sim_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &sim_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(sim_timer, TIMER_PERIOD_US));

    static display_ctx_t dctx;
    dctx.i2c_bus = i2c_bus;
    dctx.io = io_handle;
    dctx.panel = panel_handle;

    xTaskCreate(input_task, "input", 4096, NULL, 10, NULL);
    xTaskCreate(sensor_task, "ambient", 4096, NULL, 5, NULL);
    xTaskCreate(pwm_task, "pwm", 2048, NULL, 4, NULL);
    xTaskCreate(display_task, "display", 4096, &dctx, 4, NULL);
    xTaskCreate(logger_task, "logger", 4096, NULL, 3, NULL);

    log_event("Ready. Buttons: Temperature(GPIO26) Door(GPIO27) Info(GPIO14). LED PWM GPIO15. OLED I2C 21/22. DHT22 GPIO4.");
}

