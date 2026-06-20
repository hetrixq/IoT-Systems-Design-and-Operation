/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

const APP_CONFIG = {
  map: {
    // API-ключ Яндекс.Карт v3 для текущего проекта.
    apiKey: "",
    center: {
      lon: 37.6176, // Москва
      lat: 55.7558  // Москва
    },
    zoom: 10
  },
  mqtt: {
    host: "172.30.0.10",
    port: 8083,
    path: "/mqtt",
    useSSL: false,
    username: "",
    password: "",
    topic: "iot_practice/+/meteo",
    clientIdPrefix: "web_meteo_moscow_"
  }
};
