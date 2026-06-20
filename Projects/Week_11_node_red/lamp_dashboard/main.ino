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

// Подключение к учебной Wi-Fi сети.
static constexpr const char* WIFI_SSID = "Velxio-GUEST";
static constexpr const char* WIFI_PASSWORD = "";

// Локальный MQTT-брокер из docker-compose.
static constexpr const char* MQTT_BROKER_HOST = "172.30.0.10";
static constexpr uint16_t MQTT_BROKER_PORT = 1883;
static constexpr const char* MQTT_CLIENT_ID = "esp32_week11_5404";

// Аппаратные пины: индикатор статуса + RGB-каналы.
static constexpr uint8_t STATUS_LED_GPIO = 2;
static constexpr uint8_t GPIO_R = 26;
static constexpr uint8_t GPIO_G = 25;
static constexpr uint8_t GPIO_B = 33;

// Настройки ШИМ для плавного управления цветом и яркостью.
static constexpr uint8_t PWM_CH_R = 0;
static constexpr uint8_t PWM_CH_G = 1;
static constexpr uint8_t PWM_CH_B = 2;
static constexpr uint16_t PWM_FREQ_HZ = 5000;
static constexpr uint8_t PWM_RESOLUTION_BITS = 8;
static constexpr uint16_t PWM_MAX_DUTY = (1U << PWM_RESOLUTION_BITS) - 1U;

// Топики под задание Node-RED.
static constexpr const char* TOPIC_LAMP = "iot_practice/5404/lamp";
static constexpr const char* TOPIC_VALUE = "iot_practice/5404/lamp/value";
static constexpr const char* TOPIC_COLOR = "iot_practice/5404/lamp/color";
static constexpr const char* TOPIC_SCENE = "iot_practice/5404/lamp/scene";
static constexpr const char* TOPIC_LIGHT_SENSOR = "iot_practice/5404/sensor/physical_light";
static constexpr const char* SUBSCRIBE_TOPIC = "iot_practice/5404/#";

enum SceneMode : uint8_t {
  SCENE_ADAPTIVE = 0,
  SCENE_MEETING = 1,
  SCENE_RELAX = 2,
  SCENE_EMERGENCY = 3,
};

struct LampState {
  bool lampOn = true;
  int brightness = 60;
  uint8_t r = 153;
  uint8_t g = 153;
  uint8_t b = 153;
  SceneMode scene = SCENE_ADAPTIVE;
};

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
LampState state;

unsigned long lastStatusBlinkMs = 0;
unsigned long lastMqttReconnectTryMs = 0;
unsigned long lastWifiReconnectTryMs = 0;
bool wifiConnectedLogged = false;

static void writePwm(uint8_t channel, uint8_t level) {
  const uint32_t duty = static_cast<uint32_t>((level * PWM_MAX_DUTY) / 255U);
  ledcWrite(channel, duty);
}

static void applyLampState() {
  // Когда лампа выключена, принудительно опускаем все каналы в ноль.
  const uint8_t effectiveR = state.lampOn ? state.r : 0;
  const uint8_t effectiveG = state.lampOn ? state.g : 0;
  const uint8_t effectiveB = state.lampOn ? state.b : 0;

  writePwm(PWM_CH_R, effectiveR);
  writePwm(PWM_CH_G, effectiveG);
  writePwm(PWM_CH_B, effectiveB);

  Serial.printf("[PWM] lamp=%s, R=%u, G=%u, B=%u\n",
                state.lampOn ? "on" : "off",
                effectiveR,
                effectiveG,
                effectiveB);
}

static void setUniformBrightness(int percent) {
  state.brightness = constrain(percent, 0, 100);
  const uint8_t level = static_cast<uint8_t>((state.brightness * 255) / 100);
  state.r = level;
  state.g = level;
  state.b = level;
}

static void parseHexColor(const char* text) {
  if (text == nullptr || strlen(text) != 7 || text[0] != '#') {
    return;
  }

  unsigned int red = 0;
  unsigned int green = 0;
  unsigned int blue = 0;
  if (sscanf(text + 1, "%02x%02x%02x", &red, &green, &blue) == 3) {
    state.r = static_cast<uint8_t>(red);
    state.g = static_cast<uint8_t>(green);
    state.b = static_cast<uint8_t>(blue);
  }
}

static void applyScene(SceneMode scene) {
  state.scene = scene;
  switch (scene) {
    case SCENE_ADAPTIVE:
      setUniformBrightness(60);
      break;
    case SCENE_MEETING:
      state.r = 255;
      state.g = 210;
      state.b = 180;
      break;
    case SCENE_RELAX:
      state.r = 255;
      state.g = 140;
      state.b = 70;
      break;
    case SCENE_EMERGENCY:
      state.r = 255;
      state.g = 0;
      state.b = 0;
      break;
  }
}

static int convertSensorToBrightness(int sensorValue) {
  // Преобразуем освещенность в задание яркости:
  // чем больше внешний свет, тем меньше яркость лампы.
  static constexpr int SENSOR_MAX = 10000;
  int clampedSensor = constrain(sensorValue, 0, SENSOR_MAX);
  int inverted = SENSOR_MAX - clampedSensor;
  return (100 * inverted) / SENSOR_MAX;
}

static void handleTopic(const char* topic, const char* payload) {
  if (strcmp(topic, TOPIC_LAMP) == 0) {
    state.lampOn = (strcmp(payload, "off") != 0);
    Serial.printf("[MQTT] lamp=%s\n", state.lampOn ? "on" : "off");
    applyLampState();
    return;
  }

  if (strcmp(topic, TOPIC_VALUE) == 0) {
    setUniformBrightness(atoi(payload));
    Serial.printf("[MQTT] value=%d\n", state.brightness);
    applyLampState();
    return;
  }

  if (strcmp(topic, TOPIC_COLOR) == 0) {
    parseHexColor(payload);
    Serial.printf("[MQTT] color=#%02X%02X%02X\n", state.r, state.g, state.b);
    applyLampState();
    return;
  }

  if (strcmp(topic, TOPIC_SCENE) == 0) {
    if (strcmp(payload, "adaptive") == 0) {
      applyScene(SCENE_ADAPTIVE);
    } else if (strcmp(payload, "meeting") == 0) {
      applyScene(SCENE_MEETING);
    } else if (strcmp(payload, "relax") == 0) {
      applyScene(SCENE_RELAX);
    } else if (strcmp(payload, "emergency") == 0) {
      applyScene(SCENE_EMERGENCY);
    } else {
      Serial.printf("[MQTT] unknown scene=%s\n", payload);
      return;
    }

    Serial.printf("[MQTT] scene applied=%s\n", payload);
    applyLampState();
    return;
  }

  if (strcmp(topic, TOPIC_LIGHT_SENSOR) == 0) {
    int sensorValue = atoi(payload);
    int dimmerValue = convertSensorToBrightness(sensorValue);
    setUniformBrightness(dimmerValue);
    Serial.printf("[MQTT] auto-dimmer sensor=%d -> value=%d\n", sensorValue, state.brightness);
    applyLampState();
    return;
  }
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (topic == nullptr) {
    return;
  }

  char buffer[128] = {0};
  unsigned int copyLen = min(length, static_cast<unsigned int>(sizeof(buffer) - 1));
  memcpy(buffer, payload, copyLen);
  buffer[copyLen] = '\0';

  Serial.printf("[MQTT] topic=%s payload=%s\n", topic, buffer);
  handleTopic(topic, buffer);
}

static void connectWifiIfNeeded() {
  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (!wifiConnectedLogged) {
      Serial.printf("[WiFi] connected to %s, ip=%s\n", WIFI_SSID, WiFi.localIP().toString().c_str());
      wifiConnectedLogged = true;
    }
    return;
  }

  if (wifiConnectedLogged) {
    Serial.printf("[WiFi] lost connection, status=%d\n", static_cast<int>(status));
    wifiConnectedLogged = false;
  }

  unsigned long now = millis();
  if (now - lastWifiReconnectTryMs < 5000) {
    return;
  }
  lastWifiReconnectTryMs = now;

  Serial.printf("[WiFi] reconnect to %s\n", WIFI_SSID);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

static void connectMqttIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || mqttClient.connected()) {
    return;
  }

  unsigned long now = millis();
  if (now - lastMqttReconnectTryMs < 2000) {
    return;
  }
  lastMqttReconnectTryMs = now;

  if (mqttClient.connect(MQTT_CLIENT_ID)) {
    mqttClient.subscribe(SUBSCRIBE_TOPIC, 1);
    Serial.printf("[MQTT] connected, subscribe=%s\n", SUBSCRIBE_TOPIC);
  } else {
    Serial.printf("[MQTT] connect failed, rc=%d\n", mqttClient.state());
  }
}

static void updateStatusLed() {
  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  bool mqttConnected = mqttClient.connected();

  if (wifiConnected && mqttConnected) {
    digitalWrite(STATUS_LED_GPIO, HIGH);
    return;
  }

  unsigned long now = millis();
  unsigned long interval = wifiConnected ? 250 : 100;
  if (now - lastStatusBlinkMs >= interval) {
    lastStatusBlinkMs = now;
    digitalWrite(STATUS_LED_GPIO, !digitalRead(STATUS_LED_GPIO));
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED_GPIO, OUTPUT);
  digitalWrite(STATUS_LED_GPIO, LOW);

  ledcSetup(PWM_CH_R, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcSetup(PWM_CH_G, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcSetup(PWM_CH_B, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(GPIO_R, PWM_CH_R);
  ledcAttachPin(GPIO_G, PWM_CH_G);
  ledcAttachPin(GPIO_B, PWM_CH_B);

  applyScene(SCENE_ADAPTIVE);
  applyLampState();

  WiFi.mode(WIFI_STA);
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
