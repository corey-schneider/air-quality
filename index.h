#ifndef INDEX_H
#define INDEX_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Sensor Dashboard</title>
  <style>
    body { font-family: Arial; text-align: center; margin-top: 50px; }
    .sensor-box { margin: 10px; padding: 20px; border: 1px solid #ccc; display: inline-block; }
  </style>
</head>
<body>
  <h1>Sensor Dashboard</h1>
  <div class="sensor-box">
    <h2>Temperature</h2>
    <p>-- °F</p>
  </div>
  <div class="sensor-box">
    <h2>Humidity</h2>
    <p>-- %</p>
  </div>
  <div class="sensor-box">
    <h2>Pressure</h2>
    <p>-- hPa</p>
  </div>
</body>
</html>
)rawliteral";

#endif
