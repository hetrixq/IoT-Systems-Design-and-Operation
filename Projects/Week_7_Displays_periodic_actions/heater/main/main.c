/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "HEATER";

#define GPIO_BUTTON GPIO_NUM_35
#define GPIO_LED GPIO_NUM_13
#define BUTTON_ACTIVE_LEVEL 0

static const double DT = 0.5;
static const int TIMER_PERIOD_US = (int)(0.5 * 1000000);
static const int PRINT_EVERY_N = 1;

static double C_th = 200.0;
static double R_th = 1.5;
static double k_door = 18.0;

static const double P_NOM = 30.0;

static double T_target = 70.0;
static double T_ambient = 25.0;
static double T_in = 25.0;

static volatile bool door_open = false;
static volatile uint32_t tick_count = 0;
static volatile bool print_flag = false;
static volatile double last_u = 0.0;

static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;

static double kp = 0.9;
static double ki = 0.12;
static double kd = 0.04;

static double integrator = 0.0;
static double prev_error = 0.0;

static const double integrator_min = -10.0;
static const double integrator_max = 10.0;

static double clamp(double x, double lo, double hi)
{
  if (x < lo)
    return lo;
  if (x > hi)
    return hi;
  return x;
}

static double pid_compute(double setpoint, double measurement, double dt)
{
  double error = setpoint - measurement;

  integrator = clamp(integrator + error * dt,
                     integrator_min, integrator_max);

  double derivative = (error - prev_error) / dt;
  prev_error = error;

  return clamp(kp * error + ki * integrator + kd * derivative,
               0.0, 1.0);
}

static void sim_step(void)
{
  portENTER_CRITICAL(&state_mux);

  double u = pid_compute(T_target, T_in, DT);
  double W_env = (T_ambient - T_in) / R_th;
  double Q_door = door_open ? k_door * (T_in - T_ambient) : 0.0;
  double W_net = W_env + P_NOM * u - Q_door;

  T_in = clamp(T_in + (W_net / C_th) * DT, -40.0, 200.0);
  last_u = u;

  portEXIT_CRITICAL(&state_mux);

  tick_count++;
  if (tick_count % PRINT_EVERY_N == 0)
    print_flag = true;
}

static void sim_timer_cb(void *arg)
{
  (void)arg;
  sim_step();
}

static void gpio_init_all(void)
{
  gpio_config_t btn = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_INPUT,
      .pin_bit_mask = (1ULL << GPIO_BUTTON),
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
  };
  gpio_config(&btn);

  gpio_config_t led = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << GPIO_LED),
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
  };
  gpio_config(&led);
  gpio_set_level(GPIO_LED, 0);
}

static void button_task(void *arg)
{
  (void)arg;
  for (;;)
  {
    door_open = (gpio_get_level(GPIO_BUTTON) == BUTTON_ACTIVE_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void logging_task(void *arg)
{
  (void)arg;

  printf("# Heater simulation | P_NOM=%.0fW | DT=%.3fs | "
         "T_target=%.1fC | T_ambient=%.1fC\n",
         P_NOM, DT, T_target, T_ambient);
  printf("time_s,T_in_C,T_target_C,u,P_heat_W,door\n");

  ESP_LOGI(TAG, "CSV logging started. P_NOM=%.0f W  DT=%.3f s", P_NOM, DT);

  double elapsed = 0.0;

  for (;;)
  {
    if (print_flag)
    {
      double t_snap, u_snap;
      bool door_snap;

      portENTER_CRITICAL(&state_mux);
      t_snap = T_in;
      u_snap = last_u;
      door_snap = door_open;
      portEXIT_CRITICAL(&state_mux);

      elapsed += DT * PRINT_EVERY_N;

      printf("%.3f,%.4f,%.1f,%.4f,%.4f,%d\n",
             elapsed, t_snap, T_target,
             u_snap, u_snap * P_NOM,
             door_snap ? 1 : 0);

      gpio_set_level(GPIO_LED, u_snap > 0.05 ? 1 : 0);

      print_flag = false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void app_main(void)
{
  gpio_init_all();
  T_in = T_ambient;

  const esp_timer_create_args_t timer_args = {
      .callback = sim_timer_cb,
      .name = "sim_timer",
  };

  esp_timer_handle_t sim_timer;
  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &sim_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(sim_timer, TIMER_PERIOD_US));

  xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
  xTaskCreate(logging_task, "logging_task", 4096, NULL, 4, NULL);

  ESP_LOGI(TAG, "Heater simulator running | DT=%.3f s | P_NOM=%.0f W | "
                "T_target=%.1f?C",
           DT, P_NOM, T_target);
}