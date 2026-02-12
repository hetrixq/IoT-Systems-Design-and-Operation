/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// Выбираем АЦП
#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_4
#define ADC_ATTENUATION ADC_ATTEN_DB_12

void app_main(void)
{
  int adc_raw = 0;
  int voltage_mv = 0;

  adc_oneshot_unit_handle_t adc_handle;
  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = ADC_UNIT,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

  adc_oneshot_chan_cfg_t config = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = ADC_ATTENUATION,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config));

  adc_cali_handle_t cali_handle = NULL;
  bool cali_ok = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = ADC_UNIT,
      .chan = ADC_CHANNEL,
      .atten = ADC_ATTENUATION,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  if (adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) == ESP_OK)
  {
    cali_ok = true;
  }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  adc_cali_line_fitting_config_t cali_config = {
      .unit_id = ADC_UNIT,
      .atten = ADC_ATTENUATION,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  if (adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle) == ESP_OK)
  {
    cali_ok = true;
  }
#endif

  if (!cali_ok)
  {
    printf("WARN: ADC calibration not available, voltage will not be shown.\n");
  }

  while (1)
  {
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw));

    if (cali_ok)
    {
      ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, adc_raw, &voltage_mv));
      printf("ADC%d Channel[%d] Raw: %d  Voltage: %d mV (%.3f V)\n",
             ADC_UNIT + 1, ADC_CHANNEL, adc_raw,
             voltage_mv, voltage_mv / 1000.0);
    }
    else
    {
      printf("ADC%d Channel[%d] Raw: %d\n", ADC_UNIT + 1, ADC_CHANNEL, adc_raw);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  if (cali_ok)
  {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(cali_handle));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(cali_handle));
#endif
  }
  ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
}
