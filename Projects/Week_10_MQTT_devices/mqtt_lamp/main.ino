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

// Используем учебную Wi-Fi сеть из правил проекта.
static constexpr const char* WIFI_SSID = "Velxio-GUEST";
static constexpr const char* WIFI_PASSWORD = "";

// Берем IP брокера из docker-compose (сервис emqx).
static constexpr const char* MQTT_BROKER_HOST = "172.30.0.10";
static constexpr uint16_t MQTT_BROKER_PORT = 1883;
static constexpr const char* MQTT_CLIENT_ID = "5404";

static constexpr uint8_t STATUS_LED_GPIO = 2;
static constexpr uint8_t LAMP_GPIO = 4;
static constexpr uint8_t BOOT_BUTTON_GPIO = 18;
static constexpr uint8_t PWM_CHANNEL = 0;
static constexpr uint16_t PWM_FREQ_HZ = 5000;
static constexpr uint8_t PWM_RESOLUTION_BITS = 8;
static constexpr uint16_t PWM_MAX_DUTY = (1U << PWM_RESOLUTION_BITS) - 1U;

struct DeviceState {
  bool lampOn = true;
  int brightness = 50;
};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
DeviceState state;

static constexpr const char* LAMP_TOPIC = "iot_practice/5404/lamp";
static constexpr const char* VALUE_TOPIC = "iot_practice/5404/lamp/value";
static constexpr const char* SUBSCRIBE_TOPIC = "iot_practice/5404/lamp/#";

bool lastButtonLevel = HIGH;
unsigned long lastStatusBlinkMs = 0;
unsigned long lastMqttReconnectTryMs = 0;
unsigned long lastWifiReconnectTryMs = 0;
bool wifiConnectedLogged = false;

void setLampPwm(int brightnessPercent, bool lampOn) {
  int effectiveBrightness = lampOn ? brightnessPercent : 0;
  effectiveBrightness = constrain(effectiveBrightness, 0, 100);

  const uint32_t duty = static_cast<uint32_t>((effectiveBrightness * PWM_MAX_DUTY) / 100);
  ledcWrite(PWM_CHANNEL, duty);

  Serial.printf("[PWM] lamp=%s brightness=%d duty=%lu\n",
                lampOn ? "on" : "off",
                effectiveBrightness,
                static_cast<unsigned long>(duty));
}

void handleTopic(const char* topic, const char* payload) {
  if (strcmp(topic, LAMP_TOPIC) == 0) {
    const bool newLampState = (strcmp(payload, "on") == 0);
    state.lampOn = newLampState;
    setLampPwm(state.brightness, state.lampOn);
    Serial.printf("[MQTT] Команда lamp применена: %s (яркость=%d)\n", state.lampOn ? "on" : "off", state.brightness);
    return;
  }

  if (strcmp(topic, VALUE_TOPIC) == 0) {
    int value = atoi(payload);
    value = constrain(value, 0, 100);

    state.brightness = value;
    setLampPwm(state.brightness, state.lampOn);
    Serial.printf("[MQTT] Команда lamp/value применена: %d (lamp=%s)\n", state.brightness, state.lampOn ? "on" : "off");
    return;
  }

  Serial.printf("[MQTT] Неизвестная команда, топик игнорирован: %s\n", topic);
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

  // Перезапускаем подключение при каждом разрыве, чтобы лампа сама восстанавливалась
  // после перезапуска роутера/точки доступа без ручного ресета платы.
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

void processBootButton() {
  const bool currentLevel = digitalRead(BOOT_BUTTON_GPIO);

  if (lastButtonLevel == HIGH && currentLevel == LOW) {
    state.lampOn = false;
    state.brightness = 100;
    setLampPwm(state.brightness, state.lampOn);
    Serial.println("[BTN] BOOT: локальный сброс в off + brightness=100");
  }

  lastButtonLevel = currentLevel;
}

void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED_GPIO, OUTPUT);
  digitalWrite(STATUS_LED_GPIO, LOW);

  pinMode(BOOT_BUTTON_GPIO, INPUT_PULLUP);
  pinMode(LAMP_GPIO, OUTPUT);
  
  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(LAMP_GPIO, PWM_CHANNEL);
  setLampPwm(state.brightness, state.lampOn);

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

  processBootButton();
  updateStatusLed();
  delay(30);
}
