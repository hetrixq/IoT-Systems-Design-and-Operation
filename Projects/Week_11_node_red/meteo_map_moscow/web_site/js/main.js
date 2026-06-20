/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

let mapInstance = null;
const markersByStationId = {};

function setStatus(text) {
  const statusEl = document.getElementById("status");
  if (statusEl) {
    statusEl.textContent = text;
  }
}

function getColorByTemperature(value) {
  if (value <= 0) {
    return "#2563eb"; // холодно
  }
  if (value <= 20) {
    return "#16a34a"; // тепло
  }
  return "#dc2626"; // жарко
}

function getMarkerPresetByTemperature(value) {
  if (value <= 0) {
    return "islands#blueCircleDotIcon";
  }
  if (value <= 20) {
    return "islands#greenCircleDotIcon";
  }
  return "islands#redCircleDotIcon";
}

function buildBalloonContent(stationId, data) {
  const color = getColorByTemperature(data.temperature);
  const temperature = Number(data.temperature);
  const pressure = Number(data.pressure || 0);
  const lat = Number(data.lat);
  const lon = Number(data.lon);

  return `
    <div style="min-width:220px">
      <div style="padding:8px 10px; color:#fff; border-radius:6px 6px 0 0; background:${color}">
        <b>Станция ${stationId}</b>
      </div>
      <div style="padding:10px; line-height:1.45">
        <b>Город:</b> ${data.city || "Москва"}<br>
        <b>Температура:</b> ${temperature.toFixed(1)} °C<br>
        <b>Давление:</b> ${pressure.toFixed(1)} мбар<br>
        <b>Координаты:</b> ${lat.toFixed(5)}, ${lon.toFixed(5)}
      </div>
    </div>
  `;
}

function upsertStationMarker(stationId, data) {
  if (!mapInstance || !window.ymaps) {
    return;
  }

  const lat = Number(data.lat);
  const lon = Number(data.lon);
  const coords = [lat, lon];
  const markerPreset = getMarkerPresetByTemperature(Number(data.temperature));
  const balloonContent = buildBalloonContent(stationId, data);
  const iconContent = `${Math.round(Number(data.temperature))}°`;

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

function extractStationId(topic) {
  const parts = String(topic || "").split("/");
  if (parts.length < 3) {
    return "unknown_station";
  }
  return parts[parts.length - 2];
}

function handleMqttMessage(message) {
  try {
    const payload = JSON.parse(message.payloadString);
    const stationId = extractStationId(message.destinationName);
    const hasValidCoords = Number.isFinite(Number(payload.lat)) && Number.isFinite(Number(payload.lon));
    if (!hasValidCoords) {
      return;
    }
    upsertStationMarker(stationId, payload);
  } catch (error) {
    console.error("Ошибка разбора MQTT payload:", error);
  }
}

async function waitForPaho(maxAttempts, pauseMs) {
  // Ждем появления глобального объекта библиотеки, загружаемой в отдельном файле.
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
  const clientId = `${mqttCfg.clientIdPrefix}${Math.random().toString(36).slice(2)}`;
  const mqttNamespace = (window.Paho && window.Paho.MQTT) || window.PahoMQTT;
  if (!mqttNamespace) {
    setStatus("Статус MQTT: namespace Paho не найден");
    return;
  }

  const client = new mqttNamespace.Client(mqttCfg.host, mqttCfg.port, mqttCfg.path, clientId);

  client.onConnectionLost = (event) => {
    setStatus(`Статус MQTT: соединение потеряно (${event.errorMessage || "unknown"})`);
  };

  client.onMessageArrived = handleMqttMessage;

  const connectOptions = {
    useSSL: mqttCfg.useSSL,
    onSuccess: () => {
      setStatus(`Статус MQTT: подключено, подписка ${mqttCfg.topic}`);
      client.subscribe(mqttCfg.topic);
    },
    onFailure: (error) => {
      setStatus(`Статус MQTT: ошибка подключения (${error.errorMessage || "unknown"})`);
    }
  };

  if (mqttCfg.username) {
    connectOptions.userName = mqttCfg.username;
  }
  if (mqttCfg.password) {
    connectOptions.password = mqttCfg.password;
  }

  client.connect(connectOptions);
}

async function initMap() {
  if (!window.ymaps) {
    throw new Error("API 2.1 не загрузился: window.ymaps отсутствует");
  }

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

initMap().catch((error) => {
  setStatus(`Статус карты: ошибка инициализации (${error.message})`);
  console.error(error);
});
