/*
Выполнила бригада №3
Участники:
- Павлов Аркадий - ответственный за схемы
- Малков Максим - ответственный за код
- Авдеев Евгений - ответственный за отчеты
*/

const APP_CONFIG = {
  map: {
    // API-ключ Яндекс.Карт v2.1 для отдельного веб-проекта.
    apiKey: "",
    center: {
      lon: 37.6176,
      lat: 55.7558
    },
    zoom: 11
  },
  mqtt: {
    host: "172.30.0.10",
    port: 8083,
    path: "/mqtt",
    useSSL: false,
    username: "",
    password: "",
    topic: "iot_practice/+/eco",
    clientIdPrefix: "web_eco_map_5404_"
  },
  pm25Thresholds: {
    normalMax: 12.0,
    elevatedMax: 35.0
  }
};
