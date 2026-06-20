/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

// Локальный загрузчик стабильной версии Paho MQTT.
// Этот CDN-файл гарантированно публикует window.Paho.MQTT.
(function loadPahoMqtt() {
  if (window.Paho && window.Paho.MQTT) {
    return;
  }

  const script = document.createElement("script");
  script.src = "https://cdnjs.cloudflare.com/ajax/libs/paho-mqtt/1.0.1/mqttws31.min.js";
  script.async = true;
  script.onload = () => {
    if (window.Paho && window.Paho.MQTT) {
      console.log("Paho MQTT загружен (cdnjs)");
    } else {
      console.error("Paho MQTT скрипт загружен, но namespace не найден");
    }
  };
  script.onerror = () => {
    console.error("Не удалось загрузить Paho MQTT с cdnjs");
  };

  document.head.appendChild(script);
})();
