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

// Параметры учебной Wi-Fi сети (по правилам курса).
static constexpr const char* WIFI_SSID = "Velxio-GUEST";
static constexpr const char* WIFI_PASSWORD = "";

// Параметры MQTT-брокера из docker-compose (EMQX).
static constexpr const char* MQTT_BROKER_HOST = "172.30.0.10";
static constexpr uint16_t MQTT_BROKER_PORT = 1883;
static constexpr const char* MQTT_CLIENT_ID = "esp32_week11_meteo_moscow_5404";
static constexpr const char* MQTT_TOPIC_METEO = "iot_practice/5404/meteo";

// Базовые координаты Москвы (центр), используем как опорную точку.
static constexpr float MOSCOW_BASE_LAT = 55.7558f;
static constexpr float MOSCOW_BASE_LON = 37.6176f;

// Интервалы обслуживания сети и публикации телеметрии.
static constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;
static constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 2500;
static constexpr uint32_t PUBLISH_INTERVAL_MS = 5000;
static constexpr uint32_t STATUS_BLINK_INTERVAL_MS = 250;

// Встроенный светодиод ESP32 используем как индикатор состояния связи.
static constexpr uint8_t STATUS_LED_GPIO = 2;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastWifiReconnectTryMs = 0;
unsigned long lastMqttReconnectTryMs = 0;
unsigned long lastPublishMs = 0;
unsigned long lastStatusBlinkMs = 0;

bool wifiConnectedLogged = false;
bool mqttConnectedLogged = false;

// Текущее состояние "датчика": плавно меняем температуру и давление,
// чтобы карта получала правдоподобные последовательные значения.
float currentTemperature = 21.5f;
float currentPressure = 1012.0f;
float currentLat = MOSCOW_BASE_LAT;
float currentLon = MOSCOW_BASE_LON;

static void updateStatusLed() {
  const bool wifiReady = (WiFi.status() == WL_CONNECTED);
  const bool mqttReady = mqttClient.connected();

  if (wifiReady && mqttReady) {
    digitalWrite(STATUS_LED_GPIO, HIGH);
    return;
  }

  const unsigned long now = millis();
  if (now - lastStatusBlinkMs >= STATUS_BLINK_INTERVAL_MS) {
    lastStatusBlinkMs = now;
    digitalWrite(STATUS_LED_GPIO, !digitalRead(STATUS_LED_GPIO));
  }
}

static void connectWifiIfNeeded() {
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (!wifiConnectedLogged) {
      Serial.printf("[WiFi] connected to %s, ip=%s\n",
                    WIFI_SSID,
                    WiFi.localIP().toString().c_str());
      wifiConnectedLogged = true;
    }
    return;
  }

  if (wifiConnectedLogged) {
    Serial.printf("[WiFi] connection lost, status=%d\n", static_cast<int>(status));
    wifiConnectedLogged = false;
    mqttConnectedLogged = false;
  }

  const unsigned long now = millis();
  if (now - lastWifiReconnectTryMs < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }
  lastWifiReconnectTryMs = now;

  Serial.printf("[WiFi] reconnecting to %s\n", WIFI_SSID);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

static void connectMqttIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqttClient.connected()) {
    if (!mqttConnectedLogged) {
      Serial.printf("[MQTT] connected to %s:%u\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
      mqttConnectedLogged = true;
    }
    return;
  }

  const unsigned long now = millis();
  if (now - lastMqttReconnectTryMs < MQTT_RECONNECT_INTERVAL_MS) {
    return;
  }
  lastMqttReconnectTryMs = now;
  mqttConnectedLogged = false;

  Serial.printf("[MQTT] connecting, clientId=%s\n", MQTT_CLIENT_ID);
  if (mqttClient.connect(MQTT_CLIENT_ID)) {
    Serial.println("[MQTT] connect success");
  } else {
    Serial.printf("[MQTT] connect failed, rc=%d\n", mqttClient.state());
  }
}

static void updateSyntheticMeteo() {
  // Температура: меняем на небольшой шаг, удерживаем в разумных границах.
  const float tempDelta = static_cast<float>(random(-8, 9)) / 10.0f;  // [-0.8; +0.8]
  currentTemperature += tempDelta;
  if (currentTemperature < -20.0f) {
    currentTemperature = -20.0f;
  } else if (currentTemperature > 35.0f) {
    currentTemperature = 35.0f;
  }

  // Давление: небольшой дрейф в типичном диапазоне.
  const float pressureDelta = static_cast<float>(random(-5, 6)) / 10.0f;  // [-0.5; +0.5]
  currentPressure += pressureDelta;
  if (currentPressure < 980.0f) {
    currentPressure = 980.0f;
  } else if (currentPressure > 1040.0f) {
    currentPressure = 1040.0f;
  }

  // Координаты: легкая "дрожь" вокруг Москвы, чтобы видеть обновление метки.
  currentLat = MOSCOW_BASE_LAT + static_cast<float>(random(-50, 51)) / 10000.0f;
  currentLon = MOSCOW_BASE_LON + static_cast<float>(random(-50, 51)) / 10000.0f;
}

static void publishMeteoIfNeeded() {
  if (!mqttClient.connected()) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastPublishMs < PUBLISH_INTERVAL_MS) {
    return;
  }
  lastPublishMs = now;

  updateSyntheticMeteo();

  char payload[192];
  snprintf(
      payload,
      sizeof(payload),
      "{\"lon\":%.6f,\"lat\":%.6f,\"temperature\":%.1f,\"pressure\":%.1f,\"city\":\"Moscow\"}",
      currentLon,
      currentLat,
      currentTemperature,
      currentPressure);

  const bool ok = mqttClient.publish(MQTT_TOPIC_METEO, payload, false);
  Serial.printf("[MQTT] publish topic=%s payload=%s result=%s\n",
                MQTT_TOPIC_METEO,
                payload,
                ok ? "ok" : "fail");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[BOOT] Week 11 map telemetry sender (Moscow)");

  pinMode(STATUS_LED_GPIO, OUTPUT);
  digitalWrite(STATUS_LED_GPIO, LOW);

  randomSeed(static_cast<uint32_t>(esp_random()));

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
}

void loop() {
  connectWifiIfNeeded();
  connectMqttIfNeeded();

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  publishMeteoIfNeeded();
  updateStatusLed();
  delay(30);
}
