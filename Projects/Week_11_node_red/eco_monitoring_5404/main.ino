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
#include <math.h>

// Параметры учебной Wi-Fi сети из правил курса.
static constexpr const char* WIFI_SSID = "Velxio-GUEST";
static constexpr const char* WIFI_PASSWORD = "";

// MQTT-брокер EMQX из docker-compose (сервис emqx).
static constexpr const char* MQTT_BROKER_HOST = "172.30.0.10";
static constexpr uint16_t MQTT_BROKER_PORT = 1883;

// Идентификаторы устройства и топика строго по требованиям задания.
static constexpr const char* USER_ID = "5404";
static constexpr const char* DEVICE_ID = "Eco-5404";
static constexpr const char* MQTT_TOPIC_ECO = "iot_practice/5404/eco";
static constexpr const char* MQTT_CLIENT_ID = "esp32_eco_station_5404";

// Периоды переподключения и измерения.
static constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;
static constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 2500;
static constexpr uint32_t MEASURE_AND_PUBLISH_INTERVAL_MS = 5000;
static constexpr uint32_t STATUS_BLINK_INTERVAL_MS = 250;

// Аппаратные пины станции.
static constexpr uint8_t STATUS_LED_GPIO = 2;
static constexpr uint8_t TEMP_ANALOG_GPIO = 35;
static constexpr uint8_t HUMIDITY_ANALOG_GPIO = 32;
static constexpr uint8_t PM25_ANALOG_GPIO = 34;

// Статические координаты станции для отображения на карте.
static constexpr float STATION_LAT = 55.7558f;
static constexpr float STATION_LON = 37.6176f;

// Диапазон пересчета сигнала запыленности в PM2.5 (учебная калибровка).
static constexpr float PM25_MIN_UGM3 = 0.0f;
static constexpr float PM25_MAX_UGM3 = 300.0f;
static constexpr float TEMPERATURE_MIN_C = -20.0f;
static constexpr float TEMPERATURE_MAX_C = 45.0f;
static constexpr float HUMIDITY_MIN_PERCENT = 0.0f;
static constexpr float HUMIDITY_MAX_PERCENT = 100.0f;
static constexpr uint16_t ADC_MAX = 4095;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastWifiReconnectTryMs = 0;
unsigned long lastMqttReconnectTryMs = 0;
unsigned long lastMeasureAndPublishMs = 0;
unsigned long lastStatusBlinkMs = 0;

bool wifiConnectedLogged = false;
bool mqttConnectedLogged = false;

// Флаг наличия датчика давления: в текущем макете не используется и по методичке
// допускается исключение pressure при отсутствии соответствующего сенсора.
bool hasPressureSensor = false;

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

static float readPm25UgM3() {
  // Учебная модель: пересчитываем аналоговый сигнал в диапазон PM2.5.
  // Для реального пылевого датчика здесь можно подставить точную формулу из даташита.
  const int raw = analogRead(PM25_ANALOG_GPIO);
  const float ratio = static_cast<float>(raw) / static_cast<float>(ADC_MAX);
  const float pm25 = PM25_MIN_UGM3 + (PM25_MAX_UGM3 - PM25_MIN_UGM3) * ratio;
  return constrain(pm25, PM25_MIN_UGM3, PM25_MAX_UGM3);
}

static float readMappedAnalog(uint8_t pin, float minValue, float maxValue) {
  const int raw = analogRead(pin);
  const float ratio = static_cast<float>(raw) / static_cast<float>(ADC_MAX);
  const float value = minValue + (maxValue - minValue) * ratio;
  return constrain(value, minValue, maxValue);
}

static bool readSensors(float* temperature, float* humidity, float* pressure, float* pm25) {
  // Используем отдельные потенциометры как независимые источники
  // температуры, влажности и PM2.5 в эмуляторе без дополнительных библиотек.
  *temperature = readMappedAnalog(TEMP_ANALOG_GPIO, TEMPERATURE_MIN_C, TEMPERATURE_MAX_C);
  *humidity = readMappedAnalog(HUMIDITY_ANALOG_GPIO, HUMIDITY_MIN_PERCENT, HUMIDITY_MAX_PERCENT);
  *pm25 = readPm25UgM3();

  if (hasPressureSensor) {
    // Резервная точка расширения: чтение фактического давления, если датчик добавлен.
    *pressure = 1013.25f;
  } else {
    *pressure = NAN;
  }

  return true;
}

static bool buildPayload(char* outPayload, size_t outPayloadSize) {
  float temperature = NAN;
  float humidity = NAN;
  float pressure = NAN;
  float pm25 = NAN;

  if (!readSensors(&temperature, &humidity, &pressure, &pm25)) {
    return false;
  }

  if (!isfinite(temperature) || !isfinite(humidity) || !isfinite(pm25)) {
    Serial.println("[SENS] invalid numeric data");
    return false;
  }

  const unsigned long tsMs = millis();
  if (isfinite(pressure)) {
    snprintf(
        outPayload,
        outPayloadSize,
        "{\"deviceId\":\"%s\",\"userId\":\"%s\",\"ts\":%lu,\"lat\":%.6f,\"lon\":%.6f,\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f,\"pm25\":%.2f}",
        DEVICE_ID,
        USER_ID,
        tsMs,
        STATION_LAT,
        STATION_LON,
        temperature,
        humidity,
        pressure,
        pm25);
  } else {
    snprintf(
        outPayload,
        outPayloadSize,
        "{\"deviceId\":\"%s\",\"userId\":\"%s\",\"ts\":%lu,\"lat\":%.6f,\"lon\":%.6f,\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":null,\"pm25\":%.2f}",
        DEVICE_ID,
        USER_ID,
        tsMs,
        STATION_LAT,
        STATION_LON,
        temperature,
        humidity,
        pm25);
  }

  return true;
}

static void publishEcoDataIfNeeded() {
  if (!mqttClient.connected()) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastMeasureAndPublishMs < MEASURE_AND_PUBLISH_INTERVAL_MS) {
    return;
  }
  lastMeasureAndPublishMs = now;

  char payload[256];
  if (!buildPayload(payload, sizeof(payload))) {
    Serial.println("[MQTT] publish skipped: no valid sensor packet");
    return;
  }

  const bool ok = mqttClient.publish(MQTT_TOPIC_ECO, payload, false);
  Serial.printf("[MQTT] publish topic=%s payload=%s result=%s\n",
                MQTT_TOPIC_ECO,
                payload,
                ok ? "ok" : "fail");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[BOOT] Eco monitoring station (Week 11)");

  pinMode(STATUS_LED_GPIO, OUTPUT);
  digitalWrite(STATUS_LED_GPIO, LOW);

  pinMode(TEMP_ANALOG_GPIO, INPUT);
  pinMode(HUMIDITY_ANALOG_GPIO, INPUT);
  pinMode(PM25_ANALOG_GPIO, INPUT);

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

  publishEcoDataIfNeeded();
  updateStatusLed();
  delay(30);
}
