#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <sps30.h>

#include "credentials.h"  // contains: const char* ssid = "yourSSID"; const char* password = "yourPASSWORD";

Adafruit_BME680 bme;
WiFiServer server(80);

const int LED = LED_BUILTIN; // Built-in LED pin (may need to change if using Pico SDK)
const uint8_t AUTO_CLEAN_DAYS = 4;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting...");

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  Wire.begin(); // GPIO4=SDA, GPIO5=SCL for Pico W

  // --- BME680 INIT ---
  if (!bme.begin(0x76)) {
    Serial.println("BME680 not found at 0x76, trying 0x77");
    if (!bme.begin(0x77)) {
      Serial.println("Could not find a valid BME680 sensor, check wiring!");
      while (1) delay(10);
    }
  }
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);

  // --- SPS30 INIT ---
  sensirion_i2c_init();
  while (sps30_probe() != 0) {
    Serial.println("SPS30 sensor probing failed");
    delay(500);
  }
  Serial.println("SPS30 sensor probing successful");
  int16_t ret = sps30_set_fan_auto_cleaning_interval_days(AUTO_CLEAN_DAYS);
  if (ret) {
    Serial.print("SPS30 error setting auto-clean interval: ");
    Serial.println(ret);
  }
  ret = sps30_start_measurement();
  if (ret < 0) {
    Serial.println("SPS30 error starting measurement");
  }
  Serial.println("SPS30 measurements started");

  // --- WIFI INIT ---
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED, HIGH);  // LED ON
  } else {
    Serial.println("\nWiFi failed to connect.");
    digitalWrite(LED, LOW); // LED OFF
  }
  server.begin();
}

int mapToBar(float val, float minVal, float maxVal) {
  if (val <= minVal) return 0;
  if (val >= maxVal) return 10;
  return int((val - minVal) / (maxVal - minVal) * 10);
}

String getColorForValue(float val, float goodThresh, float badThresh, bool invert = false) {
  if (invert) {
    if (val >= goodThresh) return "green";
    else if (val >= badThresh) return "orange";
    else return "red";
  } else {
    if (val <= goodThresh) return "green";
    else if (val <= badThresh) return "orange";
    else return "red";
  }
}

int mapToPercent(float val, float minVal, float maxVal, bool invert = false) {
  if (val <= minVal) return invert ? 100 : 0;
  if (val >= maxVal) return invert ? 0 : 100;
  float mapped = (val - minVal) / (maxVal - minVal) * 100.0;
  return invert ? (100 - int(mapped)) : int(mapped);
}

void loop() {
  WiFiClient client = server.accept();

  if (client) {
    // Serial.println("Client connected");
    String request = client.readStringUntil('\r');
    // Serial.println("Request: " + request);
    client.flush();

    // --- BME688 READ ---
    bool bmeOk = bme.performReading();

    // --- SPS30 READ ---
    struct sps30_measurement m;
    char serial[SPS30_MAX_SERIAL_LEN];
    uint16_t data_ready;
    int16_t ret;
    bool spsOk = false;
    for (int tries = 0; tries < 5; tries++) {
      ret = sps30_read_data_ready(&data_ready);
      if (ret == 0 && data_ready) {
        ret = sps30_read_measurement(&m);
        if (ret == 0) spsOk = true;
        break;
      }
      delay(100);
    }

    // --- HTTP RESPONSE ---
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=UTF-8");
    client.println("Connection: close");
    client.println();

    client.println("<!DOCTYPE html><html><head><title>Environmental Sensor Data</title>");
    client.println("<meta charset='UTF-8'>");
    client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    client.println("</head><body>");
    client.println("<h2>Raspberry Pi Pico W Air Quality</h2>");

    // --- BME688 DATA ---
    client.println("<h3>BME688 Sensor</h3>");
    if (bmeOk) {
      float tempC = bme.temperature;
      float tempF = bme.temperature * 9.0 / 5.0 + 32.0;
      float humidity = bme.humidity;
      float pressure = bme.pressure / 100.0;
      float gas = bme.gas_resistance / 1000.0;

      client.print("Temperature: "); client.print(tempC, 1); client.println(" &deg;C<br>");
      client.print("Temperature: "); client.print(tempF, 1); client.println(" &deg;F<br>");
      client.print("Humidity: "); client.print(humidity, 1); client.println(" %<br>");
      client.print("Pressure: "); client.print(pressure, 1); client.println(" hPa<br>");
      client.print("Gas Resistance: "); client.print(gas, 1); client.println(" kΩ<br>");

      Serial.print("[BME688] Temp: ");
      Serial.print(tempC, 1); Serial.print(" C (");
      Serial.print(tempF, 1); Serial.print(" F), Humidity: ");
      Serial.print(humidity, 1); Serial.print(" %, Pressure: ");
      Serial.print(pressure, 1); Serial.print(" hPa, Gas: ");
      Serial.print(gas, 1); Serial.println(" kOhm");
    } else {
      client.println("<span style='color:red;'>BME688 reading failed!</span><br>");
      Serial.println("[BME688] Sensor read failed!");
    }

    // --- SPS30 DATA ---
    client.println("<h3>Sensirion SPS30 (Particulate Matter)</h3>");
    if (spsOk) {
      // Mass concentrations
      client.print("PM 1.0: "); client.print(m.mc_1p0, 2); client.println(" &mu;g/m³<br>");
      client.print("PM 2.5: "); client.print(m.mc_2p5, 2); client.println(" &mu;g/m³<br>");
      client.print("PM 4.0: "); client.print(m.mc_4p0, 2); client.println(" &mu;g/m³<br>");
      client.print("PM10.0: "); client.print(m.mc_10p0, 2); client.println(" &mu;g/m³<br>");
      
      Serial.print("[SPS30] PM1.0: "); Serial.print(m.mc_1p0, 2); Serial.print(" ug/m3 | ");
      Serial.print("PM2.5: "); Serial.print(m.mc_2p5, 2); Serial.print(" ug/m3 | ");
      Serial.print("PM4.0: "); Serial.print(m.mc_4p0, 2); Serial.print(" ug/m3 | ");
      Serial.print("PM10.0: "); Serial.print(m.mc_10p0, 2); Serial.println(" ug/m3");

#ifndef SPS30_LIMITED_I2C_BUFFER_SIZE
      // Number concentrations
      client.print("NC 0.5: "); client.print(m.nc_0p5, 2); client.println(" /cm³<br>");
      client.print("NC 1.0: "); client.print(m.nc_1p0, 2); client.println(" /cm³<br>");
      client.print("NC 2.5: "); client.print(m.nc_2p5, 2); client.println(" /cm³<br>");
      client.print("NC 4.0: "); client.print(m.nc_4p0, 2); client.println(" /cm³<br>");
      client.print("NC 10.0: "); client.print(m.nc_10p0, 2); client.println(" /cm³<br>");
      client.print("Typical particle size: "); client.print(m.typical_particle_size, 2); client.println(" &mu;m<br>");

      Serial.print("NC0.5: "); Serial.print(m.nc_0p5, 2); Serial.print(" /cm3 | ");
      Serial.print("NC1.0: "); Serial.print(m.nc_1p0, 2); Serial.print(" /cm3 | ");
      Serial.print("NC2.5: "); Serial.print(m.nc_2p5, 2); Serial.print(" /cm3 | ");
      Serial.print("NC4.0: "); Serial.print(m.nc_4p0, 2); Serial.print(" /cm3 | ");
      Serial.print("NC10.0: "); Serial.print(m.nc_10p0, 2); Serial.println(" /cm3");
      Serial.print("Typical particle size: "); Serial.print(m.typical_particle_size, 2); Serial.println(" um");
#endif
    } else {
      client.println("<span style='color:red;'>SPS30 reading failed!</span><br>");
      Serial.println("[SPS30] Sensor read failed!");
    }

    // --- AUTO REFRESH ---
    // client.println("<p><small>Page refreshes every 5 seconds</small></p>");
    // client.println("<script>setTimeout(() => { window.location.reload(); }, 5000);</script>");
    client.println("</body></html>");

    delay(1);
    client.stop();
    // Serial.println("Client disconnected");
  }
}