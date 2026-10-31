/*
  Smart Refrigerator Health Monitoring and Energy Optimization System
  ESP32 firmware — implements:
   1. Temperature monitoring (DS18B20)
   2. Humidity monitoring (DHT22)
   3. Door status monitoring (reed switch)
   4. Door-left-open alert (buzzer + OLED + dashboard)
   5. Cooling recovery analysis
   6. Refrigerator health score
   7. Energy waste estimation (software model, NOT measured)
   8. Predictive maintenance (recovery-time trend)
   9. Self-hosted ESP32 web dashboard
  10. OLED local display

  NOTE: Tune constants marked TUNE ME once you test on real hardware.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- WiFi ----------
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ---------- Pins ----------
#define ONE_WIRE_PIN   4
#define DHT_PIN        5
#define REED_PIN       18
#define BUZZER_PIN     19
#define OLED_SDA       21
#define OLED_SCL       22

// ---------- Thresholds (TUNE ME) ----------
const float TEMP_SAFE_MAX        = 8.0;   // °C above this = unsafe storage warning
const float RECOVERY_TARGET_TEMP = 4.0;   // °C target after door closes
const unsigned long DOOR_OPEN_ALERT_MS = 30000; // 30s door-open alert
const float ASSUMED_COMPRESSOR_KW = 0.150; // 150W assumed compressor draw (ESTIMATE)

// ---------- Objects ----------
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature tempSensor(&oneWire);
DHT dht(DHT_PIN, DHT22);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
WebServer server(80);

// ---------- State ----------
float currentTemp = 0, currentHumidity = 0;
bool doorOpen = false;
unsigned long doorOpenedAt = 0;
unsigned long doorOpenDurationTotal = 0; // today, seconds
int doorOpenCountToday = 0;

bool recoveryInProgress = false;
unsigned long recoveryStartMs = 0;
float lastRecoveryTimeSec = 0;

int healthScore = 100;
float todayEnergyWasteKWh = 0;
String maintenanceAlert = "";

unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL_MS = 10000; // 10s per your spec

// rolling temp buffer for stability score
float tempBuffer[12]; // last 2 minutes @10s
int tempBufferIdx = 0;
bool tempBufferFull = false;

// ---------- History (LittleFS) ----------
// /history.json : array of {date, avgRecovery, doorOpens, healthScore}
String todayDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "unknown";
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

void appendTodayHistory() {
  DynamicJsonDocument doc(4096);
  File f = LittleFS.open("/history.json", "r");
  if (f) {
    deserializeJson(doc, f);
    f.close();
  }
  JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

  String today = todayDate();
  // update today's entry if it exists, else append
  bool found = false;
  for (JsonVariant v : arr) {
    if (v["date"] == today) {
      v["avgRecovery"] = lastRecoveryTimeSec;
      v["doorOpens"] = doorOpenCountToday;
      v["healthScore"] = healthScore;
      found = true;
      break;
    }
  }
  if (!found) {
    JsonObject obj = arr.createNestedObject();
    obj["date"] = today;
    obj["avgRecovery"] = lastRecoveryTimeSec;
    obj["doorOpens"] = doorOpenCountToday;
    obj["healthScore"] = healthScore;
  }
  // keep only last 14 entries
  while (arr.size() > 14) arr.remove(0);

  File out = LittleFS.open("/history.json", "w");
  serializeJson(arr, out);
  out.close();
}

// checks last 3 days' avgRecovery for an increasing trend
void checkPredictiveMaintenance() {
  File f = LittleFS.open("/history.json", "r");
  if (!f) { maintenanceAlert = ""; return; }
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, f);
  f.close();
  JsonArray arr = doc.as<JsonArray>();
  int n = arr.size();
  if (n < 3) { maintenanceAlert = ""; return; }

  float r1 = arr[n-3]["avgRecovery"];
  float r2 = arr[n-2]["avgRecovery"];
  float r3 = arr[n-1]["avgRecovery"];

  if (r3 > r2 && r2 > r1 && (r3 - r1) > (0.15 * r1)) { // TUNE ME: 15% rise over 3 days
    maintenanceAlert = "Possible cooling issue — check door seal or clean condenser";
  } else {
    maintenanceAlert = "";
  }
}

// ---------- Health score ----------
void updateHealthScore() {
  // Temp stability: lower variance in recent buffer = better (40%)
  int count = tempBufferFull ? 12 : tempBufferIdx;
  float mean = 0;
  for (int i = 0; i < count; i++) mean += tempBuffer[i];
  mean = count ? mean / count : currentTemp;
  float variance = 0;
  for (int i = 0; i < count; i++) variance += pow(tempBuffer[i] - mean, 2);
  variance = count ? variance / count : 0;
  float tempScore = constrain(100 - (variance * 20), 0, 100); // TUNE ME

  // Humidity: ideal fridge range ~35-70% (20%)
  float humidityScore = (currentHumidity >= 35 && currentHumidity <= 70) ? 100 :
                          constrain(100 - abs(currentHumidity - 52) * 2, 0, 100);

  // Door usage: fewer/shorter openings = better (20%)
  float doorScore = constrain(100 - (doorOpenCountToday * 3) - (doorOpenDurationTotal / 10.0), 0, 100);

  // Recovery time: lower = better, baseline ~120s assumed healthy (20%)
  float recoveryScore = constrain(100 - ((lastRecoveryTimeSec - 120) / 2.0), 0, 100);
  if (lastRecoveryTimeSec == 0) recoveryScore = 100; // no data yet

  healthScore = round(tempScore * 0.40 + humidityScore * 0.20 + doorScore * 0.20 + recoveryScore * 0.20);
}

// ---------- Energy waste estimate ----------
// ESTIMATE ONLY: extra compressor runtime assumed proportional to excess door-open time
void estimateEnergyWaste(unsigned long doorOpenSeconds) {
  float extraRunHours = (doorOpenSeconds / 3600.0) * 1.5; // TUNE ME: assumed 1.5x runtime multiplier
  float extraKWh = extraRunHours * ASSUMED_COMPRESSOR_KW;
  todayEnergyWasteKWh += extraKWh;
}

// ---------- Sensor + door logic ----------
void readSensors() {
  tempSensor.requestTemperatures();
  currentTemp = tempSensor.getTempCByIndex(0);
  currentHumidity = dht.readHumidity();

  tempBuffer[tempBufferIdx] = currentTemp;
  tempBufferIdx = (tempBufferIdx + 1) % 12;
  if (tempBufferIdx == 0) tempBufferFull = true;

  // recovery tracking
  if (recoveryInProgress && currentTemp <= RECOVERY_TARGET_TEMP) {
    lastRecoveryTimeSec = (millis() - recoveryStartMs) / 1000.0;
    recoveryInProgress = false;
    appendTodayHistory();
    checkPredictiveMaintenance();
  }

  updateHealthScore();
}

void updateDoorState() {
  bool nowOpen = (digitalRead(REED_PIN) == HIGH); // adjust HIGH/LOW to your wiring

  if (nowOpen && !doorOpen) {
    // door just opened
    doorOpen = true;
    doorOpenedAt = millis();
    doorOpenCountToday++;
    recoveryInProgress = false; // cancel any in-progress recovery timing
  } else if (!nowOpen && doorOpen) {
    // door just closed
    doorOpen = false;
    unsigned long openDurationSec = (millis() - doorOpenedAt) / 1000;
    doorOpenDurationTotal += openDurationSec;
    estimateEnergyWaste(openDurationSec);

    // start recovery timing
    recoveryInProgress = true;
    recoveryStartMs = millis();
  }

  // door-left-open alert
  if (doorOpen && (millis() - doorOpenedAt) > DOOR_OPEN_ALERT_MS) {
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ---------- OLED ----------
void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.printf("Temp: %.1f C\n", currentTemp);
  display.printf("Humidity: %.0f %%\n", currentHumidity);
  display.printf("Door: %s\n", doorOpen ? "Open" : "Closed");
  display.printf("Health: %d %%\n", healthScore);
  if (currentTemp > TEMP_SAFE_MAX) {
    display.println("! UNSAFE TEMP !");
  }
  display.display();
}

// ---------- Web dashboard ----------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>Fridge Monitor</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{font-family:Arial;background:#111;color:#eee;padding:20px}
.card{background:#222;border-radius:10px;padding:15px;margin-bottom:10px}
.warn{color:#ff5555;font-weight:bold}
</style></head><body>
<h2>Fridge Health Dashboard</h2>
<div class="card">Temperature: <span id="temp">--</span> &deg;C</div>
<div class="card">Humidity: <span id="hum">--</span> %</div>
<div class="card">Door: <span id="door">--</span> (opened <span id="opens">--</span>x today)</div>
<div class="card">Health Score: <span id="health">--</span> %</div>
<div class="card">Last Recovery Time: <span id="recovery">--</span> s</div>
<div class="card">Estimated Energy Waste Today: <span id="energy">--</span> kWh (estimate)</div>
<div class="card warn" id="alert"></div>
<script>
async function refresh(){
  const r = await fetch('/data'); const d = await r.json();
  document.getElementById('temp').innerText = d.temp.toFixed(1);
  document.getElementById('hum').innerText = d.humidity.toFixed(0);
  document.getElementById('door').innerText = d.doorOpen ? "Open" : "Closed";
  document.getElementById('opens').innerText = d.doorOpens;
  document.getElementById('health').innerText = d.healthScore;
  document.getElementById('recovery').innerText = d.recoveryTime.toFixed(0);
  document.getElementById('energy').innerText = d.energyWaste.toFixed(3);
  document.getElementById('alert').innerText = d.maintenanceAlert;
}
setInterval(refresh, 5000);
refresh();
</script></body></html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleData() {
  DynamicJsonDocument doc(512);
  doc["temp"] = currentTemp;
  doc["humidity"] = currentHumidity;
  doc["doorOpen"] = doorOpen;
  doc["doorOpens"] = doorOpenCountToday;
  doc["healthScore"] = healthScore;
  doc["recoveryTime"] = lastRecoveryTimeSec;
  doc["energyWaste"] = todayEnergyWasteKWh;
  doc["maintenanceAlert"] = maintenanceAlert;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ---------- Setup / loop ----------
void setup() {
  Serial.begin(115200);
  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  tempSensor.begin();
  dht.begin();

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected: " + WiFi.localIP().toString());

  configTime(19800, 0, "pool.ntp.org"); // TUNE ME: 19800 = IST offset in seconds

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
}

void loop() {
  server.handleClient();

  if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = millis();
    readSensors();
    updateDoorState();
    updateOLED();
  } else {
    updateDoorState(); // check door state more frequently than 10s for accurate alert timing
  }
}
