/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Учебная Wi-Fi сеть из правил проекта.
static constexpr const char* WIFI_SSID = "Velxio-GUEST";
static constexpr const char* WIFI_PASSWORD = "";

// IP брокера EMQX из docker-compose.
static constexpr const char* MQTT_BROKER_HOST = "172.30.0.10";
static constexpr uint16_t MQTT_BROKER_PORT = 1883;

static constexpr const char* MQTT_CLIENT_ID = "esp_5404";

// Светодиод статуса Wi-Fi/MQTT на плате ESP32.
static constexpr uint8_t STATUS_LED_GPIO = 2;

// RGB-каналы: пины и номера PWM-каналов (как в исходном ESP-IDF проекте).
static constexpr uint8_t GPIO_R = 26;
static constexpr uint8_t GPIO_G = 25;
static constexpr uint8_t GPIO_B = 33;
static constexpr uint8_t PWM_CH_R = 0;
static constexpr uint8_t PWM_CH_G = 1;
static constexpr uint8_t PWM_CH_B = 2;
static constexpr uint16_t PWM_FREQ_HZ = 5000;
static constexpr uint8_t PWM_RESOLUTION_BITS = 8;
static constexpr uint16_t PWM_MAX_DUTY = (1U << PWM_RESOLUTION_BITS) - 1U;

// Топики совместимы с fake_lamp: iot_practice/esp_5404/lamp/...
static constexpr const char* LAMP_TOPIC = "iot_practice/esp_5404/lamp";
static constexpr const char* VALUE_TOPIC = "iot_practice/esp_5404/lamp/value";
static constexpr const char* COLOR_TOPIC = "iot_practice/esp_5404/lamp/color";
static constexpr const char* SCENE_TOPIC = "iot_practice/esp_5404/lamp/scene";
static constexpr const char* SUBSCRIBE_TOPIC = "iot_practice/esp_5404/lamp/#";

enum SceneMode : uint8_t {
  SCENE_ADAPTIVE = 0,
  SCENE_MEETING = 1,
  SCENE_RELAX = 2,
  SCENE_EMERGENCY = 3,
};

struct LampState {
  bool lampOn = true;
  int value = 100;
  uint8_t r = 255;
  uint8_t g = 255;
  uint8_t b = 255;
  SceneMode scene = SCENE_ADAPTIVE;
};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
LampState state;

unsigned long lastStatusBlinkMs = 0;
unsigned long lastMqttReconnectTryMs = 0;
unsigned long lastWifiReconnectTryMs = 0;
bool wifiConnectedLogged = false;

// Записываем duty на один RGB-канал (0..255 -> 8-битный ШИМ).
void writePwmChannel(uint8_t channel, uint8_t level) {
  const uint32_t duty = static_cast<uint32_t>((level * PWM_MAX_DUTY) / 255U);
  ledcWrite(channel, duty);
}

// Непосредственное применение RGB с учётом флага включения лампы.
void applyRgb(uint8_t r, uint8_t g, uint8_t b) {
  if (!state.lampOn) {
    r = 0;
    g = 0;
    b = 0;
  }
  writePwmChannel(PWM_CH_R, r);
  writePwmChannel(PWM_CH_G, g);
  writePwmChannel(PWM_CH_B, b);

  Serial.printf("[PWM] lamp=%s R=%u G=%u B=%u\n",
                state.lampOn ? "on" : "off", r, g, b);
}

// Общая яркость: одинаковый уровень на R, G и B (топик lamp/value).
void setUniformValue(int percent) {
  state.value = constrain(percent, 0, 100);
  const uint8_t level = static_cast<uint8_t>((state.value * 255) / 100);
  state.r = level;
  state.g = level;
  state.b = level;
}

// Применяем текущие уровни каналов (заданы через value или color).
void applyState() {
  if (!state.lampOn) {
    applyRgb(0, 0, 0);
    return;
  }
  applyRgb(state.r, state.g, state.b);
}

// Парсинг цвета в формате #RRGGBB.
void parseHexColor(const char* s) {
  if (strlen(s) != 7 || s[0] != '#') {
    return;
  }
  unsigned int r = 0;
  unsigned int g = 0;
  unsigned int b = 0;
  if (sscanf(s + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
    state.r = static_cast<uint8_t>(r);
    state.g = static_cast<uint8_t>(g);
    state.b = static_cast<uint8_t>(b);
  }
}

// Парсинг цвета в формате rgba(R,G,B,A); альфа игнорируется.
void parseRgbaColor(const char* s) {
  int r = 0;
  int g = 0;
  int b = 0;
  float a = 1.0f;
  if (sscanf(s, "rgba(%d,%d,%d,%f)", &r, &g, &b, &a) != 4) {
    return;
  }
  state.r = static_cast<uint8_t>(constrain(r, 0, 255));
  state.g = static_cast<uint8_t>(constrain(g, 0, 255));
  state.b = static_cast<uint8_t>(constrain(b, 0, 255));
}

// Предустановленные сцены адаптивного освещения.
void handleScene(SceneMode scene) {
  state.scene = scene;
  switch (scene) {
    case SCENE_ADAPTIVE:
      setUniformValue(60);
      break;
    case SCENE_MEETING:
      state.r = 255;
      state.g = 200;
      state.b = 180;
      break;
    case SCENE_RELAX:
      state.r = 255;
      state.g = 140;
      state.b = 60;
      break;
    case SCENE_EMERGENCY:
      state.r = 255;
      state.g = 0;
      state.b = 0;
      break;
  }
  applyState();
}

void handleTopic(const char* topic, const char* payload) {
  if (strcmp(topic, LAMP_TOPIC) == 0) {
    // Любое значение кроме "off" считается включением (как в ESP-IDF версии).
    state.lampOn = (strcmp(payload, "off") != 0);
    Serial.printf("[MQTT] lamp=%s\n", state.lampOn ? "on" : "off");
  } else if (strcmp(topic, VALUE_TOPIC) == 0) {
    setUniformValue(atoi(payload));
    Serial.printf("[MQTT] value=%d -> R=%u G=%u B=%u\n", state.value, state.r, state.g, state.b);
  } else if (strcmp(topic, COLOR_TOPIC) == 0) {
    if (strncmp(payload, "rgba(", 5) == 0) {
      parseRgbaColor(payload);
    } else if (payload[0] == '#') {
      parseHexColor(payload);
    }
    Serial.printf("[MQTT] color R=%u G=%u B=%u\n", state.r, state.g, state.b);
  } else if (strcmp(topic, SCENE_TOPIC) == 0) {
    if (strcmp(payload, "adaptive") == 0) {
      handleScene(SCENE_ADAPTIVE);
      return;
    }
    if (strcmp(payload, "meeting") == 0) {
      handleScene(SCENE_MEETING);
      return;
    }
    if (strcmp(payload, "relax") == 0) {
      handleScene(SCENE_RELAX);
      return;
    }
    if (strcmp(payload, "emergency") == 0) {
      handleScene(SCENE_EMERGENCY);
      return;
    }
    Serial.printf("[MQTT] Неизвестная сцена: %s\n", payload);
    return;
  } else {
    Serial.printf("[MQTT] Неизвестный топик: %s\n", topic);
    return;
  }
  applyState();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (topic == nullptr) {
    return;
  }

  char payloadBuffer[128] = {0};
  const unsigned int copyLen = min(length, static_cast<unsigned int>(sizeof(payloadBuffer) - 1));
  memcpy(payloadBuffer, payload, copyLen);
  payloadBuffer[copyLen] = '\0';

  Serial.printf("[MQTT] Получено: topic=%s payload=%s\n", topic, payloadBuffer);
  handleTopic(topic, payloadBuffer);
}

void connectWifiIfNeeded() {
  const wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus == WL_CONNECTED) {
    if (!wifiConnectedLogged) {
      Serial.printf("[WiFi] Подключено к %s, IP: %s\n", WIFI_SSID, WiFi.localIP().toString().c_str());
      wifiConnectedLogged = true;
    }
    return;
  }

  if (wifiConnectedLogged) {
    Serial.printf("[WiFi] Соединение потеряно, статус: %d\n", static_cast<int>(wifiStatus));
    wifiConnectedLogged = false;
  }

  const unsigned long now = millis();
  if (now - lastWifiReconnectTryMs < 5000) {
    return;
  }
  lastWifiReconnectTryMs = now;

  Serial.printf("[WiFi] Пробуем подключиться к %s...\n", WIFI_SSID);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectMqttIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || mqttClient.connected()) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastMqttReconnectTryMs < 2000) {
    return;
  }
  lastMqttReconnectTryMs = now;

  if (mqttClient.connect(MQTT_CLIENT_ID)) {
    mqttClient.subscribe(SUBSCRIBE_TOPIC, 1);
    Serial.printf("[MQTT] Подключено, подписка на %s\n", SUBSCRIBE_TOPIC);
  } else {
    Serial.printf("[MQTT] Ошибка подключения, rc=%d\n", mqttClient.state());
  }
}

void updateStatusLed() {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const bool mqttConnected = mqttClient.connected();

  if (wifiConnected && mqttConnected) {
    digitalWrite(STATUS_LED_GPIO, HIGH);
    return;
  }

  const unsigned long now = millis();
  const unsigned long interval = wifiConnected ? 250 : 100;
  if (now - lastStatusBlinkMs >= interval) {
    lastStatusBlinkMs = now;
    digitalWrite(STATUS_LED_GPIO, !digitalRead(STATUS_LED_GPIO));
  }
}

void setupRgbPwm() {
  ledcSetup(PWM_CH_R, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcSetup(PWM_CH_G, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcSetup(PWM_CH_B, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(GPIO_R, PWM_CH_R);
  ledcAttachPin(GPIO_G, PWM_CH_G);
  ledcAttachPin(GPIO_B, PWM_CH_B);
}

void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED_GPIO, OUTPUT);
  digitalWrite(STATUS_LED_GPIO, LOW);

  setupRgbPwm();

  // Стартовая сцена — адаптивное освещение (общая яркость 60%).
  handleScene(SCENE_ADAPTIVE);

  WiFi.mode(WIFI_STA);
  Serial.printf("[WiFi] Старт подключения к %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  connectWifiIfNeeded();
  connectMqttIfNeeded();

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  updateStatusLed();
  delay(30);
}
