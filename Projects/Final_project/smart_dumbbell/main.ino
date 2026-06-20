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

// Уникальный идентификатор гантели — единственный источник для MQTT-топиков.
static constexpr const char* DEVICE_ID = "db001";
// Фиксированный user_id для MVP (в проде — из RFID/клубной карты).
static constexpr const char* USER_ID = "user001";

// MQTT client id и топики формируются из DEVICE_ID при старте.
static char mqttClientId[32];
static char statusTopic[48];
static char exerciseTopic[48];
static char dockTopic[48];

// Инициализация имён топиков: smartgym/<deviceId>/status|exercise|dock.
void initMqttTopics() {
  snprintf(mqttClientId, sizeof(mqttClientId), "smartgym_%s", DEVICE_ID);
  snprintf(statusTopic, sizeof(statusTopic), "smartgym/%s/status", DEVICE_ID);
  snprintf(exerciseTopic, sizeof(exerciseTopic), "smartgym/%s/exercise", DEVICE_ID);
  snprintf(dockTopic, sizeof(dockTopic), "smartgym/%s/dock", DEVICE_ID);
}

// Пины: зелёный LED статуса, красный LED тревоги, buzzer, три кнопки.
static constexpr uint8_t STATUS_LED_GPIO = 2;
static constexpr uint8_t ALARM_LED_GPIO = 4;
static constexpr uint8_t BUTTON_START_GPIO = 18;
static constexpr uint8_t BUTTON_LIFT_GPIO = 19;
// Переключатель «гантель на месте» (для отладки вместо датчика стойки).
static constexpr uint8_t BUTTON_DOCK_GPIO = 21;
static constexpr uint8_t BUZZER_GPIO = 5;

// Антидребезг кнопки и таймауты логики MVP.
static constexpr unsigned long BUTTON_DEBOUNCE_MS = 10;
static constexpr unsigned long WIFI_ALARM_TIMEOUT_MS = 30000;
static constexpr unsigned long WORKSHIFT_IDLE_MS = 120000;
static constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 2000;
static constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;
// Периодическая телеметрия status и dock (плюс мгновенная отправка при изменениях).
static constexpr unsigned long STATUS_HEARTBEAT_MS = 30000;
static constexpr unsigned long DOCK_HEARTBEAT_MS = 30000;
// Локальная тревога после N неудачных попыток connect() к брокеру (Wi-Fi при этом есть).
static constexpr uint8_t MQTT_CONNECT_MAX_RETRIES = 5;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Состояние сессии и счётчики для отладки в Serial.
struct DumbbellState {
  bool inWorkshift = false;
  bool onDock = false;
  uint32_t totalReps = 0;
  unsigned long lastActivityMs = 0;
};

DumbbellState state;

// Состояние антидребезга для каждой кнопки (INPUT_PULLUP).
struct ButtonState {
  bool lastLevel = HIGH;
  unsigned long lastChangeMs = 0;
  bool wasPressed = false;
};

ButtonState startButton;
ButtonState liftButton;
ButtonState dockButton;
unsigned long lastStatusBlinkMs = 0;
unsigned long lastStatusPublishMs = 0;
unsigned long lastDockPublishMs = 0;
unsigned long lastMqttReconnectTryMs = 0;
unsigned long lastWifiReconnectTryMs = 0;
unsigned long wifiDisconnectedSinceMs = 0;
unsigned long lastAlarmToggleMs = 0;
bool wifiConnectedLogged = false;
bool mqttConnectedLogged = false;
bool localAlarmActive = false;
uint8_t mqttConnectFailCount = 0;
bool mqttBrokerAlarmLatched = false;

void stopLocalAlarm();
void resetMqttBrokerAlarmState();

// Сборка JSON для топика status (deviceId уже в пути топика).
void buildStatusPayload(char* buffer, size_t bufferSize, const char* statusValue) {
  if (strcmp(statusValue, "workshift") == 0) {
    snprintf(buffer, bufferSize,
             "{\"status\":\"workshift\",\"user_id\":\"%s\"}",
             USER_ID);
    return;
  }
  snprintf(buffer, bufferSize, "{\"status\":\"%s\"}", statusValue);
}

// Сборка JSON для события lift_rep в топике exercise.
void buildExercisePayload(char* buffer, size_t bufferSize) {
  snprintf(buffer, bufferSize, "{\"event\":\"lift_rep\",\"rep_delta\":1}");
}

// Сборка JSON для топика dock — гантель на стойке или нет.
void buildDockPayload(char* buffer, size_t bufferSize, bool onDock) {
  snprintf(buffer, bufferSize, "{\"on_dock\":%s}", onDock ? "true" : "false");
}

// Текущее значение status для heartbeat и reconnect.
const char* currentStatusValue() {
  return state.inWorkshift ? "workshift" : "online";
}

// Публикация status с retain=true, чтобы Node-RED видел последнее состояние.
bool publishStatus(const char* statusValue) {
  if (!mqttClient.connected()) {
    return false;
  }

  char payload[96];
  buildStatusPayload(payload, sizeof(payload), statusValue);

  const bool ok = mqttClient.publish(statusTopic, payload, true);
  Serial.printf("[MQTT] status publish: %s -> %s\n", statusValue, ok ? "ok" : "fail");
  if (ok) {
    lastStatusPublishMs = millis();
  }
  return ok;
}

// Публикация состояния «на месте» с retain=true.
bool publishDock(bool onDock) {
  if (!mqttClient.connected()) {
    return false;
  }

  char payload[48];
  buildDockPayload(payload, sizeof(payload), onDock);

  const bool ok = mqttClient.publish(dockTopic, payload, true);
  Serial.printf("[MQTT] dock publish: on_dock=%s -> %s\n",
                onDock ? "true" : "false",
                ok ? "ok" : "fail");
  if (ok) {
    lastDockPublishMs = millis();
  }
  return ok;
}

// Периодический heartbeat status — дублирует текущее состояние.
void publishStatusHeartbeatIfDue() {
  if (!mqttClient.connected()) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastStatusPublishMs < STATUS_HEARTBEAT_MS) {
    return;
  }

  publishStatus(currentStatusValue());
}

// Периодический heartbeat dock — дублирует текущее состояние стойки.
void publishDockHeartbeatIfDue() {
  if (!mqttClient.connected()) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastDockPublishMs < DOCK_HEARTBEAT_MS) {
    return;
  }

  publishDock(state.onDock);
}

// Публикация одного повторения упражнения.
bool publishLiftRep() {
  if (!mqttClient.connected()) {
    return false;
  }

  char payload[64];
  buildExercisePayload(payload, sizeof(payload));

  const bool ok = mqttClient.publish(exerciseTopic, payload, false);
  Serial.printf("[MQTT] exercise lift_rep -> %s (total=%lu)\n",
                ok ? "ok" : "fail",
                static_cast<unsigned long>(state.totalReps));
  return ok;
}

// Начало или завершение тренировки по кнопке start (toggle workshift ↔ online).
void toggleWorkshift() {
  if (state.inWorkshift) {
    if (publishStatus("online")) {
      state.inWorkshift = false;
      Serial.println("[SESSION] workshift завершён вручную, status=online");
    }
    return;
  }

  if (publishStatus("workshift")) {
    state.inWorkshift = true;
    state.lastActivityMs = millis();
    Serial.printf("[SESSION] workshift начат, user_id=%s\n", USER_ID);
  }
}

// Завершение сессии по таймауту простоя — возвращаем status online.
void endWorkshiftIfIdle() {
  if (!state.inWorkshift || state.lastActivityMs == 0) {
    return;
  }

  const unsigned long now = millis();
  if (now - state.lastActivityMs < WORKSHIFT_IDLE_MS) {
    return;
  }

  if (publishStatus("online")) {
    state.inWorkshift = false;
    Serial.println("[SESSION] workshift завершён по простою, status=online");
  }
}

// Восстановление Wi-Fi с авто-reconnect, как в mqtt_lamp.
void connectWifiIfNeeded() {
  const wl_status_t wifiStatus = WiFi.status();

  if (wifiStatus == WL_CONNECTED) {
    if (!wifiConnectedLogged) {
      Serial.printf("[WiFi] Подключено к %s, IP: %s\n",
                    WIFI_SSID,
                    WiFi.localIP().toString().c_str());
      wifiConnectedLogged = true;
      wifiDisconnectedSinceMs = 0;
      resetMqttBrokerAlarmState();
      stopLocalAlarm();
    }
    return;
  }

  if (wifiConnectedLogged) {
    Serial.printf("[WiFi] Соединение потеряно, статус: %d\n", static_cast<int>(wifiStatus));
    wifiConnectedLogged = false;
    mqttConnectedLogged = false;
    if (wifiDisconnectedSinceMs == 0) {
      wifiDisconnectedSinceMs = millis();
    }
  }

  const unsigned long now = millis();
  if (now - lastWifiReconnectTryMs < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }
  lastWifiReconnectTryMs = now;

  Serial.printf("[WiFi] Пробуем подключиться к %s...\n", WIFI_SSID);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// Подключение к MQTT с LWT offline на топике status.
void connectMqttIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || mqttClient.connected()) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastMqttReconnectTryMs < MQTT_RECONNECT_INTERVAL_MS) {
    return;
  }
  lastMqttReconnectTryMs = now;

  // LWT: брокер опубликует offline при внезапной потере соединения (анти-кража).
  char lwtPayload[128];
  buildStatusPayload(lwtPayload, sizeof(lwtPayload), "offline");

  if (mqttClient.connect(mqttClientId, statusTopic, 1, true, lwtPayload)) {
    Serial.println("[MQTT] Подключено к брокеру");
    mqttConnectedLogged = true;
    resetMqttBrokerAlarmState();
    stopLocalAlarm();
    // После reconnect восстанавливаем актуальный status и dock для серверной логики.
    publishStatus(currentStatusValue());
    publishDock(state.onDock);
  } else {
    mqttConnectFailCount++;
    if (mqttConnectFailCount >= MQTT_CONNECT_MAX_RETRIES) {
      mqttBrokerAlarmLatched = true;
    }
    Serial.printf("[MQTT] Ошибка подключения, rc=%d (попытка %u/%u)\n",
                  mqttClient.state(),
                  mqttConnectFailCount,
                  MQTT_CONNECT_MAX_RETRIES);
  }
}

void resetMqttBrokerAlarmState() {
  mqttConnectFailCount = 0;
  mqttBrokerAlarmLatched = false;
}

// Обслуживание MQTT-сессии: loop() и фиксация обрыва без сброса счётчика ретраев.
void maintainMqttSession() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!mqttClient.connected()) {
    if (mqttConnectedLogged) {
      mqttConnectedLogged = false;
      Serial.println("[MQTT] Соединение с брокером потеряно");
    }
    return;
  }

  // loop() возвращает false, если TCP/MQTT-сессия оборвалась (брокер остановлен и т.п.).
  if (!mqttClient.loop()) {
    Serial.println("[MQTT] loop(): сессия оборвана, пробуем переподключиться");
    mqttClient.disconnect();
    mqttConnectedLogged = false;
  }
}

// Гасим buzzer и красный LED, если тревога больше не нужна.
void stopLocalAlarm() {
  if (!localAlarmActive) {
    digitalWrite(BUZZER_GPIO, LOW);
    digitalWrite(ALARM_LED_GPIO, LOW);
    return;
  }
  localAlarmActive = false;
  digitalWrite(BUZZER_GPIO, LOW);
  digitalWrite(ALARM_LED_GPIO, LOW);
}

// Wi-Fi недоступен дольше порога — условие локальной тревоги.
bool isWifiAlarmCondition() {
  if (WiFi.status() == WL_CONNECTED) {
    return false;
  }
  if (wifiDisconnectedSinceMs == 0) {
    return false;
  }
  return (millis() - wifiDisconnectedSinceMs) >= WIFI_ALARM_TIMEOUT_MS;
}

// Брокер недоступен после исчерпания ретраев — условие локальной тревоги.
bool isMqttAlarmCondition() {
  return WiFi.status() == WL_CONNECTED && mqttBrokerAlarmLatched;
}

// Локальная тревога: buzzer + красный LED (потеря Wi-Fi или брокера).
void updateLocalAlarm() {
  const bool alarmNeeded = isWifiAlarmCondition() || isMqttAlarmCondition();

  if (!alarmNeeded) {
    stopLocalAlarm();
    return;
  }

  if (!localAlarmActive) {
    localAlarmActive = true;
    digitalWrite(ALARM_LED_GPIO, LOW);
    digitalWrite(BUZZER_GPIO, LOW);
    if (isWifiAlarmCondition()) {
      Serial.println("[ALARM] Wi-Fi недоступен дольше порога — buzzer + красный LED");
    } else {
      Serial.println("[ALARM] Брокер недоступен после ретраев — buzzer + красный LED");
    }
  }

  const unsigned long now = millis();
  if (now - lastAlarmToggleMs >= 150) {
    lastAlarmToggleMs = now;
    const bool alarmOn = !digitalRead(BUZZER_GPIO);
    digitalWrite(BUZZER_GPIO, alarmOn ? HIGH : LOW);
    digitalWrite(ALARM_LED_GPIO, alarmOn ? HIGH : LOW);
  }
}

// Индикация связи: постоянный LED при Wi-Fi+MQTT, иначе мигание.
void updateStatusLed() {
  if (localAlarmActive) {
    return;
  }

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

// Универсальная обработка одной кнопки с антидребезгом.
bool processDebouncedPress(uint8_t gpio, ButtonState& btn, unsigned long now) {
  const bool currentLevel = digitalRead(gpio);

  if (currentLevel != btn.lastLevel) {
    btn.lastChangeMs = now;
    btn.lastLevel = currentLevel;
  }

  if (now - btn.lastChangeMs < BUTTON_DEBOUNCE_MS) {
    return false;
  }

  if (currentLevel == LOW && !btn.wasPressed) {
    btn.wasPressed = true;
    return true;
  }

  if (currentLevel == HIGH) {
    btn.wasPressed = false;
  }

  return false;
}

// Кнопка start — toggle тренировки: start → workshift, повтор → online.
void processStartButton() {
  const unsigned long now = millis();
  if (!processDebouncedPress(BUTTON_START_GPIO, startButton, now)) {
    return;
  }
  Serial.println("[BTN] start: переключение состояния тренировки");
  toggleWorkshift();
}

// Кнопка lift — один подъём = одно событие lift_rep (только в активной сессии).
void processLiftButton() {
  const unsigned long now = millis();
  if (!processDebouncedPress(BUTTON_LIFT_GPIO, liftButton, now)) {
    return;
  }

  if (!state.inWorkshift) {
    Serial.println("[BTN] lift: игнорирован — сначала нажмите start");
    return;
  }

  state.totalReps++;
  state.lastActivityMs = now;
  publishLiftRep();
  Serial.printf("[BTN] lift: подъём #%lu\n", static_cast<unsigned long>(state.totalReps));
}

// Кнопка dock — переключатель «гантель на месте» (отладка вместо датчика).
void processDockButton() {
  const unsigned long now = millis();
  if (!processDebouncedPress(BUTTON_DOCK_GPIO, dockButton, now)) {
    return;
  }

  state.onDock = !state.onDock;
  publishDock(state.onDock);
  Serial.printf("[BTN] dock: on_dock=%s\n", state.onDock ? "true" : "false");
}

void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED_GPIO, OUTPUT);
  pinMode(ALARM_LED_GPIO, OUTPUT);
  pinMode(BUTTON_START_GPIO, INPUT_PULLUP);
  pinMode(BUTTON_LIFT_GPIO, INPUT_PULLUP);
  pinMode(BUTTON_DOCK_GPIO, INPUT_PULLUP);
  pinMode(BUZZER_GPIO, OUTPUT);
  digitalWrite(STATUS_LED_GPIO, LOW);
  digitalWrite(ALARM_LED_GPIO, LOW);
  digitalWrite(BUZZER_GPIO, LOW);

  initMqttTopics();

  WiFi.mode(WIFI_STA);
  Serial.printf("[BOOT] Smart dumbbell %s, user_id=%s\n", DEVICE_ID, USER_ID);
  Serial.printf("[MQTT] status=%s exercise=%s dock=%s\n",
                statusTopic, exerciseTopic, dockTopic);
  Serial.printf("[WiFi] Старт подключения к %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  mqttClient.setBufferSize(512);
}

void loop() {
  connectWifiIfNeeded();
  maintainMqttSession();
  connectMqttIfNeeded();

  if (mqttClient.connected()) {
    endWorkshiftIfIdle();
    publishStatusHeartbeatIfDue();
    publishDockHeartbeatIfDue();
  }

  processStartButton();
  processLiftButton();
  processDockButton();
  updateLocalAlarm();
  updateStatusLed();
  delay(10);
}
