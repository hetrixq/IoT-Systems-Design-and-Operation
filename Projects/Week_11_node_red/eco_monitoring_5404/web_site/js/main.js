/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

let mapInstance = null;
const markersByStationId = {};
let mqttClient = null;

function setStatus(text) {
  const statusEl = document.getElementById("status");
  if (statusEl) {
    statusEl.textContent = text;
  }
}

function getPm25State(pm25Value) {
  const value = Number(pm25Value);
  if (!Number.isFinite(value)) {
    return "unknown";
  }
  if (value <= APP_CONFIG.pm25Thresholds.normalMax) {
    return "normal";
  }
  if (value <= APP_CONFIG.pm25Thresholds.elevatedMax) {
    return "elevated";
  }
  return "critical";
}

function getMarkerPresetByPm25(pm25Value) {
  const state = getPm25State(pm25Value);
  if (state === "normal") {
    return "islands#greenCircleDotIcon";
  }
  if (state === "elevated") {
    return "islands#yellowCircleDotIcon";
  }
  if (state === "critical") {
    return "islands#redCircleDotIcon";
  }
  return "islands#grayCircleDotIcon";
}

function getPm25Label(pm25Value) {
  const state = getPm25State(pm25Value);
  if (state === "normal") {
    return "Норма";
  }
  if (state === "elevated") {
    return "Повышенный";
  }
  if (state === "critical") {
    return "Критический";
  }
  return "Нет данных";
}

function toFixedOrDash(value, digits) {
  const numberValue = Number(value);
  if (!Number.isFinite(numberValue)) {
    return "н/д";
  }
  return numberValue.toFixed(digits);
}

function formatTimestamp(payloadTs, receivedAtMs) {
  // У ESP32 в этом проекте нет NTP-часов, поэтому надежно показываем
  // фактическое серверное/браузерное время получения телеметрии.
  const date = new Date(receivedAtMs);
  const hh = String(date.getHours()).padStart(2, "0");
  const mm = String(date.getMinutes()).padStart(2, "0");
  const ss = String(date.getSeconds()).padStart(2, "0");
  const dd = String(date.getDate()).padStart(2, "0");
  const mo = String(date.getMonth() + 1).padStart(2, "0");
  const yyyy = date.getFullYear();

  if (Number.isFinite(Number(payloadTs))) {
    return `${dd}.${mo}.${yyyy} ${hh}:${mm}:${ss} (получено, ts=${payloadTs})`;
  }
  return `${dd}.${mo}.${yyyy} ${hh}:${mm}:${ss}`;
}

function buildBalloonContent(stationId, data) {
  return `
    <div style="min-width:280px">
      <div class="balloon-title">Станция ${stationId}</div>
      <table class="balloon-table">
        <tr><td>Температура, °C</td><td>${toFixedOrDash(data.temperature, 1)}</td></tr>
        <tr><td>Влажность, %</td><td>${toFixedOrDash(data.humidity, 1)}</td></tr>
        <tr><td>Давление, гПа</td><td>${toFixedOrDash(data.pressure, 1)}</td></tr>
        <tr><td>PM2.5, мкг/м³</td><td>${toFixedOrDash(data.pm25, 1)}</td></tr>
        <tr><td>Уровень PM2.5</td><td>${getPm25Label(data.pm25)}</td></tr>
        <tr><td>Последнее обновление</td><td>${formatTimestamp(data.ts, data.receivedAtMs)}</td></tr>
      </table>
    </div>
  `;
}

function resolveStationId(topic, payload) {
  if (payload && typeof payload.deviceId === "string" && payload.deviceId.trim()) {
    return payload.deviceId.trim();
  }

  const parts = String(topic || "").split("/");
  if (parts.length >= 3) {
    return `Eco-${parts[1]}`;
  }
  return "Eco-unknown";
}

function upsertStationMarker(stationId, payload) {
  if (!mapInstance || !window.ymaps) {
    return;
  }

  const lat = Number(payload.lat);
  const lon = Number(payload.lon);
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
    return;
  }

  const coords = [lat, lon];
  const markerPreset = getMarkerPresetByPm25(payload.pm25);
  const iconContent = Number.isFinite(Number(payload.pm25))
    ? `${Math.round(Number(payload.pm25))}`
    : "?";

  const data = {
    temperature: payload.temperature,
    humidity: payload.humidity,
    pressure: payload.pressure,
    pm25: payload.pm25,
    ts: payload.ts,
    receivedAtMs: Date.now()
  };

  const balloonContent = buildBalloonContent(stationId, data);

  if (!markersByStationId[stationId]) {
    const placemark = new ymaps.Placemark(
      coords,
      {
        iconContent,
        hintContent: `Станция ${stationId}`,
        balloonContent
      },
      {
        preset: markerPreset
      }
    );
    mapInstance.geoObjects.add(placemark);
    markersByStationId[stationId] = placemark;
    return;
  }

  const placemark = markersByStationId[stationId];
  placemark.geometry.setCoordinates(coords);
  placemark.properties.set("iconContent", iconContent);
  placemark.properties.set("hintContent", `Станция ${stationId}`);
  placemark.properties.set("balloonContent", balloonContent);
  placemark.options.set("preset", markerPreset);
}

function handleMqttMessage(message) {
  try {
    const payload = JSON.parse(message.payloadString);
    const stationId = resolveStationId(message.destinationName, payload);
    upsertStationMarker(stationId, payload);
  } catch (error) {
    console.error("Ошибка разбора MQTT payload:", error);
  }
}

async function waitForPaho(maxAttempts, pauseMs) {
  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    if ((window.Paho && window.Paho.MQTT) || window.PahoMQTT) {
      return true;
    }
    await new Promise((resolve) => setTimeout(resolve, pauseMs));
  }
  return false;
}

function connectMqtt() {
  const mqttCfg = APP_CONFIG.mqtt;
  const mqttNamespace = (window.Paho && window.Paho.MQTT) || window.PahoMQTT;
  if (!mqttNamespace) {
    setStatus("Статус MQTT: namespace Paho не найден");
    return;
  }

  const clientId = `${mqttCfg.clientIdPrefix}${Math.random().toString(36).slice(2)}`;
  mqttClient = new mqttNamespace.Client(mqttCfg.host, mqttCfg.port, mqttCfg.path, clientId);

  mqttClient.onConnectionLost = (event) => {
    setStatus(`Статус MQTT: соединение потеряно (${event.errorMessage || "unknown"})`);
    setTimeout(connectMqtt, 3000);
  };

  mqttClient.onMessageArrived = handleMqttMessage;

  const connectOptions = {
    useSSL: mqttCfg.useSSL,
    onSuccess: () => {
      setStatus(`Статус MQTT: подключено, подписка ${mqttCfg.topic}`);
      mqttClient.subscribe(mqttCfg.topic);
    },
    onFailure: (error) => {
      setStatus(`Статус MQTT: ошибка подключения (${error.errorMessage || "unknown"})`);
      setTimeout(connectMqtt, 3000);
    }
  };

  if (mqttCfg.username) {
    connectOptions.userName = mqttCfg.username;
  }
  if (mqttCfg.password) {
    connectOptions.password = mqttCfg.password;
  }

  mqttClient.connect(connectOptions);
}

function loadYandexMapsApi() {
  return new Promise((resolve, reject) => {
    if (window.ymaps) {
      resolve();
      return;
    }

    if (!APP_CONFIG.map.apiKey) {
      reject(new Error("Не заполнен APP_CONFIG.map.apiKey"));
      return;
    }

    const script = document.createElement("script");
    script.src = `https://api-maps.yandex.ru/2.1/?lang=ru_RU&apikey=${encodeURIComponent(APP_CONFIG.map.apiKey)}`;
    script.async = true;
    script.onload = () => resolve();
    script.onerror = () => reject(new Error("Не удалось загрузить API Яндекс.Карт v2.1"));
    document.head.appendChild(script);
  });
}

async function initMapAndMqtt() {
  await loadYandexMapsApi();
  await new Promise((resolve) => ymaps.ready(resolve));

  mapInstance = new ymaps.Map("map", {
    center: [APP_CONFIG.map.center.lat, APP_CONFIG.map.center.lon],
    zoom: APP_CONFIG.map.zoom
  });

  const pahoReady = await waitForPaho(150, 200);
  if (!pahoReady) {
    setStatus("Статус MQTT: библиотека Paho не загружена");
    return;
  }

  connectMqtt();
}

initMapAndMqtt().catch((error) => {
  setStatus(`Ошибка инициализации: ${error.message}`);
  console.error(error);
});
