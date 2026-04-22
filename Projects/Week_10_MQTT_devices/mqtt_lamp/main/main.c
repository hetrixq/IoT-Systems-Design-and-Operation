#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define MQTT_BROKER_URI "mqtt://broker.hivemq.com"
#define USER_ID "student01"

#define STATUS_LED_GPIO GPIO_NUM_2
#define LAMP_GPIO GPIO_NUM_4
#define BOOT_BUTTON_GPIO GPIO_NUM_0

#define PWM_MODE LEDC_LOW_SPEED_MODE
#define PWM_TIMER LEDC_TIMER_0
#define PWM_CHANNEL LEDC_CHANNEL_0
#define PWM_FREQ_HZ 5000
#define PWM_RES LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY ((1 << PWM_RES) - 1)

typedef struct {
    bool lamp_on;
    int brightness;
    bool wifi_connected;
    bool mqtt_connected;
} device_state_t;

static const char *TAG = "mqtt_lamp";
static esp_mqtt_client_handle_t s_mqtt = NULL;
static device_state_t s_state = {
    .lamp_on = false,
    .brightness = 100,
    .wifi_connected = false,
    .mqtt_connected = false,
};

static void set_status_led(bool on)
{
    gpio_set_level(STATUS_LED_GPIO, on ? 1 : 0);
}

static void set_lamp_pwm(int brightness_percent, bool lamp_on)
{
    int effective = lamp_on ? brightness_percent : 0;
    if (effective < 0) {
        effective = 0;
    }
    if (effective > 100) {
        effective = 100;
    }
    uint32_t duty = (uint32_t)((effective * PWM_MAX_DUTY) / 100);
    ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty);
    ledc_update_duty(PWM_MODE, PWM_CHANNEL);
}

static void publish_state(void)
{
    if (!s_mqtt || !s_state.mqtt_connected) {
        return;
    }
    const char *base = "iot_practice/" USER_ID;
    esp_mqtt_client_publish(s_mqtt, base "/lamp", s_state.lamp_on ? "on" : "off", 0, 1, 1);

    char brightness[8];
    snprintf(brightness, sizeof(brightness), "%d", s_state.brightness);
    esp_mqtt_client_publish(s_mqtt, base "/lamp/value", brightness, 0, 1, 1);
}

static void status_task(void *arg)
{
    bool led_on = false;
    while (1) {
        if (!s_state.wifi_connected) {
            led_on = !led_on;
            set_status_led(led_on);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!s_state.mqtt_connected) {
            led_on = !led_on;
            set_status_led(led_on);
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        set_status_led(true);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void button_task(void *arg)
{
    int last = 1;
    while (1) {
        int current = gpio_get_level(BOOT_BUTTON_GPIO);
        if (last == 1 && current == 0) {
            s_state.lamp_on = false;
            s_state.brightness = 100;
            set_lamp_pwm(s_state.brightness, s_state.lamp_on);
            publish_state();
            ESP_LOGI(TAG, "Boot button pressed: reset to off + brightness=100");
        }
        last = current;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_state.wifi_connected = false;
        s_state.mqtt_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_state.wifi_connected = true;
        ESP_LOGI(TAG, "Wi-Fi connected");
    }
}

static void handle_topic(const char *topic, const char *payload)
{
    const char *base = "iot_practice/" USER_ID;
    char lamp_topic[64];
    char value_topic[64];
    snprintf(lamp_topic, sizeof(lamp_topic), "%s/lamp", base);
    snprintf(value_topic, sizeof(value_topic), "%s/lamp/value", base);

    if (strcmp(topic, lamp_topic) == 0) {
        s_state.lamp_on = (strcmp(payload, "on") == 0);
        set_lamp_pwm(s_state.brightness, s_state.lamp_on);
        publish_state();
        return;
    }
    if (strcmp(topic, value_topic) == 0) {
        int val = atoi(payload);
        if (val < 0) {
            val = 0;
        }
        if (val > 100) {
            val = 100;
        }
        s_state.brightness = val;
        set_lamp_pwm(s_state.brightness, s_state.lamp_on);
        publish_state();
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    if (event_id == MQTT_EVENT_CONNECTED) {
        s_state.mqtt_connected = true;
        char topic[64];
        snprintf(topic, sizeof(topic), "iot_practice/%s/lamp/#", USER_ID);
        esp_mqtt_client_subscribe(s_mqtt, topic, 1);
        publish_state();
        ESP_LOGI(TAG, "MQTT connected, subscribed to %s", topic);
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        s_state.mqtt_connected = false;
    } else if (event_id == MQTT_EVENT_DATA) {
        char topic[128] = {0};
        char payload[128] = {0};
        memcpy(topic, event->topic, event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1);
        memcpy(payload, event->data, event->data_len < (int)sizeof(payload) - 1 ? event->data_len : (int)sizeof(payload) - 1);
        handle_topic(topic, payload);
    }
}

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };
    s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt);
}

static void gpio_ledc_init(void)
{
    gpio_config_t status_conf = {
        .pin_bit_mask = 1ULL << STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&status_conf);
    set_status_led(false);

    gpio_config_t button_conf = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&button_conf);

    ledc_timer_config_t timer_cfg = {
        .speed_mode = PWM_MODE,
        .timer_num = PWM_TIMER,
        .duty_resolution = PWM_RES,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t channel_cfg = {
        .gpio_num = LAMP_GPIO,
        .speed_mode = PWM_MODE,
        .channel = PWM_CHANNEL,
        .timer_sel = PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&channel_cfg);
    set_lamp_pwm(s_state.brightness, s_state.lamp_on);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    gpio_ledc_init();
    wifi_init();
    mqtt_init();

    xTaskCreate(status_task, "status_task", 2048, NULL, 4, NULL);
    xTaskCreate(button_task, "button_task", 2048, NULL, 4, NULL);
}
