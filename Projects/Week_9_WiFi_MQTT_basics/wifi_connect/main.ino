/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

#include <WiFi.h>
#include <cstring>

// Параметры беспроводной сети для подключения контроллера.
static constexpr const char *WIFI_SSID = "Velxio-GUEST";
static constexpr const char *WIFI_PASSWORD = "";
// Максимальное число попыток переподключения после потери соединения.
static constexpr int MAX_RETRY_ATTEMPTS = 10;

// Глобальные флаги состояния:
// s_retry_num - сколько раз пытались переподключиться подряд;
// s_successful_connections - счётчик успешных получений IP;
// s_connected - есть ли активное подключение прямо сейчас;
// s_failed - достигли ли лимита попыток и считаем ли подключение неуспешным.
static int s_retry_num = 0;
static uint32_t s_successful_connections = 0;
static bool s_connected = false;
static bool s_failed = false;

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  (void)info;

  if (event == ARDUINO_EVENT_WIFI_STA_START) {
    // Станция запущена - сразу инициируем первичное подключение.
    Serial.printf("Wi-Fi started, trying to connect to SSID: %s\n", WIFI_SSID);
    if (strlen(WIFI_PASSWORD) == 0) {
      WiFi.begin(WIFI_SSID);
    } else {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    return;
  }

  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    // Если связь была активной и оборвалась - фиксируем факт отключения.
    if (s_connected) {
      s_connected = false;
      Serial.println("Disconnected from AP");
    }

    // Пытаемся восстановить соединение, пока не достигнут лимит попыток.
    if (s_retry_num < MAX_RETRY_ATTEMPTS) {
      s_retry_num++;
      Serial.printf("Reconnect attempt %d/%d\n", s_retry_num, MAX_RETRY_ATTEMPTS);
      WiFi.reconnect();
    } else {
      // Лимит исчерпан: отмечаем ошибочное состояние, чтобы вывести его в loop().
      s_failed = true;
      Serial.printf("Failed to connect after %d attempts\n", MAX_RETRY_ATTEMPTS);
    }
    return;
  }

  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    // Успешное подключение: сбрасываем retry-счётчик и обновляем флаги.
    s_retry_num = 0;
    s_connected = true;
    s_failed = false;
    s_successful_connections++;

    // Выводим текущий адрес и статистику успешных подключений.
    Serial.print("Got IP: ");
    Serial.println(WiFi.localIP());
    Serial.printf("Successful connection count: %lu\n",
                  static_cast<unsigned long>(s_successful_connections));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();

  // Переводим Wi-Fi в режим станции (подключение к существующей точке доступа).
  WiFi.mode(WIFI_MODE_STA);
  // Регистрируем обработчик асинхронных Wi-Fi событий.
  WiFi.onEvent(onWiFiEvent);

  Serial.printf("wifi_init_sta finished\n");
  Serial.printf("Connecting to AP: %s\n", WIFI_SSID);

  // Запускаем первую попытку подключения из setup().
  if (strlen(WIFI_PASSWORD) == 0) {
    WiFi.begin(WIFI_SSID);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void loop() {
  // Единоразово сообщаем о факте успешного подключения.
  if (s_connected) {
    static bool reported_connected = false;
    if (!reported_connected) {
      Serial.printf("Connected to AP: %s\n", WIFI_SSID);
      reported_connected = true;
    }
  } else {
    // Единоразово сообщаем об окончательной неудаче (после всех попыток).
    static bool reported_fail = false;
    if (s_failed && !reported_fail) {
      Serial.printf("Could not connect to AP: %s\n", WIFI_SSID);
      reported_fail = true;
    }
    // Если состояние "failed" снято (например, после следующего успеха),
    // разрешаем выводить сообщение об ошибке повторно в будущем.
    if (!s_failed) {
      reported_fail = false;
    }
  }

  // Небольшая пауза, чтобы не перегружать цикл и Serial-лог.
  delay(250);
}
