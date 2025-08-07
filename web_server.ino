#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>

#include "credentials.h"  // contains: const char* ssid = "yourSSID"; const char* password = "yourPASSWORD";

Adafruit_BME680 bme;

WiFiServer server(80);
const int LED = LED_BUILTIN;  // Use built-in LED pin

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting...");

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  Wire.begin();  // Fixed pins GPIO4=SDA, GPIO5=SCL on Pico W

  if (!bme.begin(0x76)) {
    Serial.println("BME680 not found at 0x76, trying 0x77");
    if (!bme.begin(0x77)) {
      Serial.println("Could not find a valid BME680 sensor, check wiring!");
      while (1) delay(10);
    }
  }

  // Configure sensor settings
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);

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
    digitalWrite(LED, HIGH);  // Turn on LED on successful WiFi connection
  } else {
    Serial.println("\nWiFi failed to connect.");
    digitalWrite(LED, LOW); // LED off or handle failure indication
  }

  server.begin();
}

// Function to map a float value to a 0-10 scale for bars
int mapToBar(float val, float minVal, float maxVal) {
  if (val <= minVal) return 0;
  if (val >= maxVal) return 10;
  return int((val - minVal) / (maxVal - minVal) * 10);
}

// Function to get color based on value and thresholds
String getColorForValue(float val, float goodThresh, float badThresh, bool invert = false) {
  // invert true means lower val is better (e.g. gas resistance inverted)
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

void sendBar(WiFiClient &client, int filled) {
  // Sends a simple horizontal bar with 'filled' blocks out of 10
  client.print("<span style='font-family: monospace;'>");
  for (int i = 0; i < filled; i++) client.print("&#9608;");  // full block ▇
  for (int i = filled; i < 10; i++) client.print("&#9617;");  // light block ░
  client.println("</span>");
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
    Serial.println("Client connected");
    String request = client.readStringUntil('\r');
    Serial.println("Request: " + request);
    client.flush();

    // Perform sensor reading
    if (!bme.performReading()) {
      Serial.println("Sensor read failed!");
    }

    // Prepare data strings
    // float tempF = bme.temperature * 9.0 / 5.0 + 32.0;
    // float humidity = bme.humidity;
    // float pressure = bme.pressure / 100.0;
    // float gas = bme.gas_resistance / 1000.0;

    // Send HTTP headers
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=UTF-8");
    client.println("Connection: close");
    client.println();

    float tempF = bme.temperature * 9.0 / 5.0 + 32.0;
    float humidity = bme.humidity;
    float pressure = bme.pressure / 100.0;
    float gas = bme.gas_resistance / 1000.0;

    // Map bars
    int tempBar = mapToBar(tempF, 65, 85);          // Ideal temp 65-85°F
    int humBar = mapToBar(humidity, 30, 60);        // Ideal humidity 30-60%
    int pressBar = mapToBar(pressure, 990, 1030);   // Normal sea level pressure approx
    int gasBar = mapToBar(gas, 0, 50);              

    // Get colors
    String tempColor = getColorForValue(tempF, 72, 80);
    String humColor = getColorForValue(humidity, 40, 60);
    String pressColor = getColorForValue(pressure, 1000, 1020);
    String gasColor = getColorForValue(gas, 30, 10, true);  // invert for gas resistance


int tempPercent = mapToPercent(tempF, 65, 85, false);
int humPercent = mapToPercent(humidity, 30, 60, false);
int pressPercent = mapToPercent(pressure, 990, 1030, false);
int gasPercent = mapToPercent(gas, 10, 30, true);  // invert for gas

client.println("<!DOCTYPE html><html><head><title>BME688 Sensor Data</title>");
client.println("<meta charset='UTF-8'>");
client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
// client.println("<style>");
// client.println(".bar-container { position: relative; width: 90vw; max-width: 600px; height: 25px; margin: 15px 0; background: linear-gradient(to right, red 0%, orange 50%, green 100%); border-radius: 8px; }");
// client.println(".bar-marker { position: absolute; top: -5px; width: 4px; height: 35px; background: black; border-radius: 2px; }");
// client.println(".bar-labels { display: flex; justify-content: space-between; font-size: 0.9em; margin-top: 4px; font-weight: 600; color: #555; }");
// client.println("</style>");
client.println("</head><body>");

client.println("<h2>Indoor Air Quality Sensor (BME688)</h2>");

// Temperature
// client.println("<p><b>Temperature:</b> " + String(tempF,1) + " °F</p>");
// client.println("<div class='bar-container'>");
// client.println("<div class='bar-marker' style='left:" + String(tempPercent) + "%;'></div>");
// client.println("</div>");
// client.println("<div class='bar-labels'><span>Bad (65°F)</span><span>Good (85°F)</span></div>");

// // Humidity
// client.println("<p><b>Humidity:</b> " + String(humidity,1) + " %</p>");
// client.println("<div class='bar-container'>");
// client.println("<div class='bar-marker' style='left:" + String(humPercent) + "%;'></div>");
// client.println("</div>");
// client.println("<div class='bar-labels'><span>Bad (30%)</span><span>Good (60%)</span></div>");

// // Pressure
// client.println("<p><b>Pressure:</b> " + String(pressure,1) + " hPa</p>");
// client.println("<div class='bar-container'>");
// client.println("<div class='bar-marker' style='left:" + String(pressPercent) + "%;'></div>");
// client.println("</div>");
// client.println("<div class='bar-labels'><span>Bad (990 hPa)</span><span>Good (1030 hPa)</span></div>");

// // Gas Resistance (VOC)
// client.println("<p><b>Gas Resistance (VOC indicator):</b> " + String(gas,1) + " kΩ</p>");
// client.println("<div class='bar-container'>");
// client.println("<div class='bar-marker' style='left:" + String(gasPercent) + "%;'></div>");
// client.println("</div>");
// client.println("<div class='bar-labels'><span>Bad (&lt;10 kΩ)</span><span>Good (&gt;30 kΩ)</span></div>");

// // Page refresh
// client.println("<p><small>Page refreshes every 5 seconds</small></p>");
// client.println("<script>setTimeout(() => { window.location.reload(); }, 5000);</script>");
// client.println("</body></html>");

client.print("Temperature: ");
client.print(bme.temperature);
client.println(" &deg;C<br>");

client.print("Humidity: ");
client.print(bme.humidity);
client.println(" %<br>");

client.print("Pressure: ");
client.print(bme.pressure / 100.0);
client.println(" hPa<br>");

client.print("Gas Resistance: ");
client.print(bme.gas_resistance / 1000.0);
client.println(" kΩ<br>");

client.println("</body></html>");


    delay(1);
    client.stop();
    Serial.println("Client disconnected");
  }
}
