/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

#include <WiFi.h>
#include <cerrno>
#include <cstring>

extern "C" {
#include "lwip/inet.h"
#include "lwip/sockets.h"
}

// Параметры беспроводной сети для подключения контроллера.
static constexpr const char *WIFI_SSID = "Velxio-GUEST";
static constexpr const char *WIFI_PASSWORD = "";
// Максимальное число попыток переподключения после потери соединения.
static constexpr int MAX_RETRY_ATTEMPTS = 10;

// Параметры TCP-сервера для обмена данными.
static constexpr const char *SERVER_HOST = "172.30.0.20";
static constexpr uint16_t SERVER_PORT = 12345;
static constexpr uint32_t SOCKET_RETRY_DELAY_MS = 2000;
static constexpr uint32_t SOCKET_SEND_PERIOD_MS = 2000;

// Глобальные флаги состояния:
// s_retry_num - сколько раз пытались переподключиться подряд;
// s_successful_connections - счётчик успешных получений IP;
// s_connected - есть ли активное подключение прямо сейчас;
// s_failed - достигли ли лимита попыток и считаем ли подключение неуспешным.
static int s_retry_num = 0;
static uint32_t s_successful_connections = 0;
static volatile bool s_connected = false;
static volatile bool s_failed = false;

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

static bool resolveServerAddress(struct sockaddr_in *dest_addr) {
  dest_addr->sin_family = AF_INET;
  dest_addr->sin_port = htons(SERVER_PORT);

  // Принимаем только прямой IPv4-адрес, чтобы полностью исключить DNS.
  if (inet_pton(AF_INET, SERVER_HOST, &dest_addr->sin_addr) == 1) {
    return true;
  }

  Serial.printf("Invalid static IPv4 address in SERVER_HOST: %s\n", SERVER_HOST);
  return false;
}

static void socketTask(void *arg) {
  (void)arg;

  char tx_buf[64];
  char rx_buf[128];

  for (;;) {
    // Пока Wi-Fi не подключен, задача просто спит и ждёт следующей проверки.
    while (!s_connected) {
      vTaskDelay(pdMS_TO_TICKS(500));
    }

    Serial.println("[sock] WiFi ready, creating socket");

    const int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
      Serial.printf("[sock] Unable to create socket, errno=%d\n", errno);
      vTaskDelay(pdMS_TO_TICKS(SOCKET_RETRY_DELAY_MS));
      continue;
    }

    struct sockaddr_in dest_addr = {};
    if (!resolveServerAddress(&dest_addr)) {
      close(sock);
      vTaskDelay(pdMS_TO_TICKS(SOCKET_RETRY_DELAY_MS));
      continue;
    }

    if (connect(sock, reinterpret_cast<struct sockaddr *>(&dest_addr), sizeof(dest_addr)) != 0) {
      Serial.printf("[sock] Socket connect failed, errno=%d\n", errno);
      close(sock);
      vTaskDelay(pdMS_TO_TICKS(SOCKET_RETRY_DELAY_MS));
      continue;
    }

    // Таймаут на recv() нужен, чтобы не зависнуть навсегда, если сервер молчит.
    struct timeval recv_timeout = {};
    recv_timeout.tv_sec = 2;
    recv_timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));

    Serial.println("[sock] Socket connected");

    // Основной цикл обмена: отправляем строку и пытаемся прочитать ответ сервера.
    while (s_connected) {
      snprintf(tx_buf, sizeof(tx_buf), "Hello from ESP32\n");

      const int send_result = send(sock, tx_buf, strlen(tx_buf), 0);
      if (send_result < 0) {
        Serial.printf("[sock] Send failed, errno=%d\n", errno);
        break;
      }

      const int len = recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
      if (len < 0) {
        // Для таймаута читаем как штатную ситуацию: сервер пока ничего не прислал.
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
          Serial.printf("[sock] Recv failed, errno=%d\n", errno);
          break;
        }
      } else if (len == 0) {
        Serial.println("[sock] Server closed connection");
        break;
      } else {
        rx_buf[len] = '\0';
        Serial.printf("[sock] Received: %s\n", rx_buf);
      }

      vTaskDelay(pdMS_TO_TICKS(SOCKET_SEND_PERIOD_MS));
    }

    Serial.println("[sock] WiFi lost or socket error, closing socket");
    close(sock);
    vTaskDelay(pdMS_TO_TICKS(SOCKET_RETRY_DELAY_MS));
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

  Serial.println("wifi_init_sta finished");
  Serial.printf("Connecting to AP: %s\n", WIFI_SSID);

  // Запускаем первую попытку подключения из setup().
  if (strlen(WIFI_PASSWORD) == 0) {
    WiFi.begin(WIFI_SSID);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  // Аналог app_main() для Arduino-скетча: запускаем отдельную задачу TCP-клиента.
  xTaskCreate(socketTask, "socket_task", 4096, nullptr, 5, nullptr);
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
