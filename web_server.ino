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

String pmCategory(float val, float pmtype) {
  if (pmtype == 2.5) {
    if (val <= 5.0) return "EXCELLENT";
    else if (val <= 12.0) return "GOOD";
    else if (val <= 35.4) return "MODERATE";
    else if (val <= 55.4) return "POOR";
    else return "HAZARDOUS";
  } else { // PM10 & fallback
    if (val <= 10.0) return "EXCELLENT";
    else if (val <= 54.0) return "GOOD";
    else if (val <= 154.0) return "MODERATE";
    else if (val <= 254.0) return "POOR";
    else return "HAZARDOUS";
  }
}

String pmColor(const String& category) {
  if (category == "EXCELLENT") return "#2ecc40";   // green
  if (category == "GOOD")     return "#0074d9";    // blue
  if (category == "MODERATE") return "#ffdc00";    // yellow
  if (category == "POOR")     return "#ff851b";    // orange
  if (category == "HAZARDOUS")return "#ff4136";    // red
  return "#111"; // default dark gray
}

String pmHtml(float val, float pmtype) {
  String cat = pmCategory(val, pmtype);
  String color = pmColor(cat);
  String label = "PM" + String(pmtype,1);
  char buf[32];
  snprintf(buf, sizeof(buf), "%.2f", val);
  String html = label + ": <span style='color:" + color + ";font-weight:bold'>" + buf + " (" + cat + ")</span> &mu;g/m³<br>";
  return html;
}

String ncCategory(float val) {
  if (val <= 10.0) return "EXCELLENT";
  else if (val <= 50.0) return "GOOD";
  else if (val <= 100.0) return "MODERATE";
  else if (val <= 300.0) return "POOR";
  else return "HAZARDOUS";
}

String ncColor(const String& category) {
  // Use same color palette as PM
  return pmColor(category);
}

String ncHtml(const String& label, float val) {
  String cat = ncCategory(val);
  String color = ncColor(cat);
  char buf[32];
  snprintf(buf, sizeof(buf), "%.2f", val);
  String html = label + ": <span style='color:" + color + ";font-weight:bold'>" + buf + " (" + cat + ")</span> /cm³<br>";
  return html;
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
    client.println("<style>");
    client.println("body { font-family: Arial, sans-serif; background:#f5f5f5; color:#222; }");
    client.println("h2 { color: #0074d9; }");
    client.println("table.aqref { border-collapse: collapse; margin-top:1em; }");
    client.println("table.aqref td, table.aqref th { border:1px solid #ccc;padding:2px 8px; font-size:0.9em; }");
    client.println("table.aqref th { background: #eee; }");
    client.println("</style>");
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
      client.print(pmHtml(m.mc_1p0, 1.0));
      client.print(pmHtml(m.mc_2p5, 2.5));
      client.print(pmHtml(m.mc_4p0, 4.0));
      client.print(pmHtml(m.mc_10p0, 10.0));
      
      Serial.print("[SPS30] PM1.0: "); Serial.print(m.mc_1p0, 2); Serial.print(" ug/m3 | ");
      Serial.print("PM2.5: "); Serial.print(m.mc_2p5, 2); Serial.print(" ug/m3 | ");
      Serial.print("PM4.0: "); Serial.print(m.mc_4p0, 2); Serial.print(" ug/m3 | ");
      Serial.print("PM10.0: "); Serial.print(m.mc_10p0, 2); Serial.println(" ug/m3");

#ifndef SPS30_LIMITED_I2C_BUFFER_SIZE
      // Number concentrations
      client.print(ncHtml("NC 0.5", m.nc_0p5));
      client.print(ncHtml("NC 1.0", m.nc_1p0));
      client.print(ncHtml("NC 2.5", m.nc_2p5));
      client.print(ncHtml("NC 4.0", m.nc_4p0));
      client.print(ncHtml("NC 10.0", m.nc_10p0));
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

    // --- AIR QUALITY REFERENCE TABLE ---
    client.println("<hr><h4>Air Quality Reference Ranges</h4>");
    client.println("<table class='aqref'>");
    client.println("<tr><th>Category</th><th>PM2.5 (&mu;g/m³)</th><th>PM10 (&mu;g/m³)</th><th>NC (particles/cm³)</th></tr>");
    client.println("<tr><td style='color:#2ecc40;font-weight:bold'>EXCELLENT</td><td>0–5</td><td>0–10</td><td>0–10</td></tr>");
    client.println("<tr><td style='color:#0074d9;font-weight:bold'>GOOD</td><td>5.1–12</td><td>10.1–54</td><td>10.1–50</td></tr>");
    client.println("<tr><td style='color:#ffdc00;font-weight:bold'>MODERATE</td><td>12.1–35.4</td><td>55–154</td><td>50.1–100</td></tr>");
    client.println("<tr><td style='color:#ff851b;font-weight:bold'>POOR</td><td>35.5–55.4</td><td>155–254</td><td>100.1–300</td></tr>");
    client.println("<tr><td style='color:#ff4136;font-weight:bold'>HAZARDOUS</td><td>55.5+</td><td>255+</td><td>300+</td></tr>");
    client.println("</table>");
    client.println("<p style='font-size:0.9em;color:#555'>PM reference: US EPA &amp; WHO guidelines. NC ranges are research-based, not regulatory.<br>");
    client.println("‘NC’ = number concentration of particles per cubic centimeter.</p>");


    // --- AUTO REFRESH ---
    // client.println("<p><small>Page refreshes every 5 seconds</small></p>");
    // client.println("<script>setTimeout(() => { window.location.reload(); }, 5000);</script>");
    client.println("</body></html>");

    delay(1);
    client.stop();
    // Serial.println("Client disconnected");
  }
}