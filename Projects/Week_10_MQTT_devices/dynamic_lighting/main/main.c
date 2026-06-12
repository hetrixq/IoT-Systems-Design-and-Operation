#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/ledc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define MQTT_BROKER_URI "mqtt://broker.hivemq.com"
#define USER_ID "student01"

#define CH_R LEDC_CHANNEL_0
#define CH_G LEDC_CHANNEL_1
#define CH_B LEDC_CHANNEL_2
#define GPIO_R GPIO_NUM_26
#define GPIO_G GPIO_NUM_25
#define GPIO_B GPIO_NUM_33
#define PWM_MODE LEDC_LOW_SPEED_MODE
#define PWM_TIMER LEDC_TIMER_0
#define PWM_RES LEDC_TIMER_8_BIT
#define PWM_FREQ 5000

typedef enum {
    MODE_DIMMER = 0,
    MODE_RGB = 1,
} lamp_mode_t;

typedef enum {
    SCENE_ADAPTIVE = 0,
    SCENE_MEETING = 1,
    SCENE_RELAX = 2,
    SCENE_EMERGENCY = 3,
} scene_mode_t;

typedef struct {
    bool lamp_on;
    lamp_mode_t mode;
    int value;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    scene_mode_t scene;
} lamp_state_t;

static const char *TAG = "dynamic_lighting";
static esp_mqtt_client_handle_t s_mqtt = NULL;
static lamp_state_t s_state = {
    .lamp_on = true,
    .mode = MODE_DIMMER,
    .value = 100,
    .r = 255,
    .g = 255,
    .b = 255,
    .scene = SCENE_ADAPTIVE,
};

static uint32_t duty8(uint8_t x)
{
    return x;
}

static void apply_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_state.lamp_on) {
        r = 0;
        g = 0;
        b = 0;
    }
    ledc_set_duty(PWM_MODE, CH_R, duty8(r));
    ledc_set_duty(PWM_MODE, CH_G, duty8(g));
    ledc_set_duty(PWM_MODE, CH_B, duty8(b));
    ledc_update_duty(PWM_MODE, CH_R);
    ledc_update_duty(PWM_MODE, CH_G);
    ledc_update_duty(PWM_MODE, CH_B);
}

static void apply_state(void)
{
    if (!s_state.lamp_on) {
        apply_rgb(0, 0, 0);
        return;
    }
    if (s_state.mode == MODE_DIMMER) {
        uint8_t level = (uint8_t)((s_state.value * 255) / 100);
        apply_rgb(level, level, level);
        return;
    }
    apply_rgb(s_state.r, s_state.g, s_state.b);
}

static void parse_hex_color(const char *s)
{
    if (strlen(s) == 7 && s[0] == '#') {
        unsigned int r = 0, g = 0, b = 0;
        if (sscanf(s + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            s_state.r = (uint8_t)r;
            s_state.g = (uint8_t)g;
            s_state.b = (uint8_t)b;
        }
    }
}

static void parse_rgba_color(const char *s)
{
    int r = 0, g = 0, b = 0;
    float a = 1.0f;
    if (sscanf(s, "rgba(%d,%d,%d,%f)", &r, &g, &b, &a) == 4) {
        if (r < 0) r = 0;
        if (g < 0) g = 0;
        if (b < 0) b = 0;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        s_state.r = (uint8_t)r;
        s_state.g = (uint8_t)g;
        s_state.b = (uint8_t)b;
    }
}

static void handle_scene(scene_mode_t scene)
{
    s_state.scene = scene;
    if (scene == SCENE_ADAPTIVE) {
        s_state.mode = MODE_DIMMER;
        s_state.value = 60;
    } else if (scene == SCENE_MEETING) {
        s_state.mode = MODE_RGB;
        s_state.r = 255; s_state.g = 200; s_state.b = 180;
    } else if (scene == SCENE_RELAX) {
        s_state.mode = MODE_RGB;
        s_state.r = 255; s_state.g = 140; s_state.b = 60;
    } else if (scene == SCENE_EMERGENCY) {
        s_state.mode = MODE_RGB;
        s_state.r = 255; s_state.g = 0; s_state.b = 0;
    }
    apply_state();
}

static void handle_message(const char *topic, const char *payload)
{
    const char *base = "iot_practice/" USER_ID;
    char t_lamp[64], t_mode[64], t_value[64], t_color[64], t_scene[64];
    snprintf(t_lamp, sizeof(t_lamp), "%s/lamp", base);
    snprintf(t_mode, sizeof(t_mode), "%s/lamp/mode", base);
    snprintf(t_value, sizeof(t_value), "%s/lamp/value", base);
    snprintf(t_color, sizeof(t_color), "%s/lamp/color", base);
    snprintf(t_scene, sizeof(t_scene), "%s/lamp/scene", base);

    if (strcmp(topic, t_lamp) == 0) {
        s_state.lamp_on = strcmp(payload, "off") != 0;
    } else if (strcmp(topic, t_mode) == 0) {
        s_state.mode = (strcmp(payload, "rgb") == 0) ? MODE_RGB : MODE_DIMMER;
    } else if (strcmp(topic, t_value) == 0) {
        int v = atoi(payload);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        s_state.value = v;
    } else if (strcmp(topic, t_color) == 0) {
        if (strncmp(payload, "rgba(", 5) == 0) {
            parse_rgba_color(payload);
        } else if (payload[0] == '#') {
            parse_hex_color(payload);
        }
    } else if (strcmp(topic, t_scene) == 0) {
        if (strcmp(payload, "adaptive") == 0) handle_scene(SCENE_ADAPTIVE);
        if (strcmp(payload, "meeting") == 0) handle_scene(SCENE_MEETING);
        if (strcmp(payload, "relax") == 0) handle_scene(SCENE_RELAX);
        if (strcmp(payload, "emergency") == 0) handle_scene(SCENE_EMERGENCY);
    }
    apply_state();
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    if (event_id == MQTT_EVENT_CONNECTED) {
        char t[64];
        snprintf(t, sizeof(t), "iot_practice/%s/lamp/#", USER_ID);
        esp_mqtt_client_subscribe(s_mqtt, t, 1);
        ESP_LOGI(TAG, "Subscribed: %s", t);
    } else if (event_id == MQTT_EVENT_DATA) {
        char topic[128] = {0};
        char payload[128] = {0};
        int tl = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        int dl = event->data_len < (int)sizeof(payload) - 1 ? event->data_len : (int)sizeof(payload) - 1;
        memcpy(topic, event->topic, tl);
        memcpy(payload, event->data, dl);
        handle_message(topic, payload);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
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

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

static void ledc_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = PWM_MODE,
        .timer_num = PWM_TIMER,
        .duty_resolution = PWM_RES,
        .freq_hz = PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);

    ledc_channel_config_t c = {
        .speed_mode = PWM_MODE,
        .timer_sel = PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .duty = 0,
        .hpoint = 0,
    };
    c.channel = CH_R; c.gpio_num = GPIO_R; ledc_channel_config(&c);
    c.channel = CH_G; c.gpio_num = GPIO_G; ledc_channel_config(&c);
    c.channel = CH_B; c.gpio_num = GPIO_B; ledc_channel_config(&c);
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };
    s_mqtt = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ledc_init();
    wifi_init();
    mqtt_init();
    handle_scene(SCENE_ADAPTIVE);
}
