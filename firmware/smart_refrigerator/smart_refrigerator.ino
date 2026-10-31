// ================================================================
// SMART REFRIGERATOR HEALTH MONITORING & ENERGY OPTIMIZATION
// ESP32 SOFTWARE PROTOTYPE - FINAL VERSION (PREMIUM DASHBOARD)
//
// FINAL PROJECT SCOPE
// 1. Temperature Monitoring & Intelligence
// 2. Humidity Monitoring
// 3. Door Status & Door Intelligence
// 4. Cooling Recovery Analysis
// 5. Refrigerator Health Score
// 6. Estimated Energy Consumption (kWh)
// 7. Predictive Maintenance
// 8. Local Web Dashboard (redesigned, self-contained, no CDN)
// 9. Persistent Software Day Counter
// 10. Historical Data using LittleFS
// 11. Live temperature trend sparkline (RAM ring buffer)
// 12. 7-day health score history chart (from LittleFS)
// 13. Live activity feed (door/recovery/maintenance events)
// 14. Daily min/max temperature + device uptime
//
// HEALTH SCORE
// Temperature Stability = 40%
// Door Behavior         = 20%
// Cooling Recovery      = 20%
// Energy Efficiency     = 20%
//
// STORAGE
// Preferences/NVS -> Day counter
// LittleFS         -> Daily history
//
// NETWORK
// ESP32 SoftAP -> Local dashboard
// NO NTP, NO RTC, NO INTERNET REQUIRED
// IMPORTANT: Dashboard has NO external CDN/font/script dependencies,
// because clients on a SoftAP have no internet uplink. Everything is
// inlined so the page always loads.
//
// IMPORTANT
// Energy consumption is MODEL-BASED estimated kWh.
// It is NOT direct electrical measurement.
// Reference power = 120 W, Base duty cycle = 0.45
//
// Food Freshness is NOT part of the final project.
// ================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================================================================
// WIFI
// ================================================================
const char* AP_SSID = "Smart_Refrigerator";
const char* AP_PASSWORD = "refrigerator";
WebServer server(80);

// ================================================================
// STORAGE
// ================================================================
Preferences preferences;
const char* HISTORY_FILE = "/history.json";
const int MAX_HISTORY_RECORDS = 14;

// ================================================================
// HARDWARE PINS
// ================================================================
#define DS18B20_PIN 4
#define DHT_PIN 16
#define DHT_TYPE DHT22
#define DOOR_PIN 17
#define BUZZER_PIN 25
#define OLED_SDA_PIN 21
#define OLED_SCL_PIN 22
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

const unsigned long REAL_DAY_DURATION_MS = 86400000UL;
const unsigned long DEMO_DAY_DURATION_MS = 120000UL; // legacy compatibility
const bool DEMO_MODE = false;

OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

bool oledReady = false;
bool temperatureSensorReady = false;
bool dhtSensorReady = false;
bool temperatureReadingValid = false;
bool humidityReadingValid = false;
unsigned long lastDhtRead = 0UL;
unsigned long lastTemperatureRequest = 0UL;
unsigned long lastOledUpdate = 0UL;
const unsigned long SENSOR_INTERVAL_MS = 2000UL;
const unsigned long OLED_INTERVAL_MS = 1000UL;
const unsigned long SENSOR_STALE_MS = 10000UL;
unsigned long lastValidTemperatureMillis = 0UL;
unsigned long lastValidHumidityMillis = 0UL;

// ================================================================
// SENSOR VALUES
// ================================================================
float refrigeratorTemperature = 4.2f;
float humidity = 62.0f;
bool doorOpen = false;
bool previousDoorState = false;

// ================================================================
// TEMPERATURE SETTINGS
// ================================================================
const float TARGET_TEMPERATURE = 4.0f;
const float RECOVERY_TEMPERATURE = 4.5f;
const float TEMP_WARNING_LIMIT = 6.0f;
const float TEMP_HIGH_LIMIT = 8.0f;

// ================================================================
// DOOR SETTINGS
// ================================================================
const unsigned long DOOR_WARNING_SECONDS = 30UL;
const unsigned long DOOR_ALERT_SECONDS = 60UL;

// ================================================================
// RECOVERY SETTINGS
// ================================================================
const unsigned long GOOD_RECOVERY_SECONDS = 120UL;
const unsigned long MODERATE_RECOVERY_SECONDS = 240UL;

// ================================================================
// ENERGY MODEL
// ================================================================
const float REFERENCE_POWER_W = 120.0f;
const float BASE_DUTY_CYCLE = 0.45f;

// ================================================================
// TIMING
// ================================================================
unsigned long previousMillis = 0UL;
unsigned long lastSimulationUpdate = 0UL;
unsigned long lastPreferenceSave = 0UL;

// ================================================================
// SOFTWARE DAY
// ================================================================
int dayNumber = 1;
unsigned long simulatedElapsedMs = 0UL;
unsigned long dailyElapsedMs = 0UL;

// ================================================================
// DAILY DOOR STATISTICS
// ================================================================
unsigned int dailyDoorOpenings = 0;
unsigned long dailyDoorOpenDurationSeconds = 0UL;
unsigned int dailyLongDoorOpenings = 0;
unsigned long dailyLongestDoorOpenSeconds = 0UL;
unsigned long currentDoorOpenSeconds = 0UL;

// ================================================================
// DAILY RECOVERY STATISTICS
// ================================================================
unsigned long dailyRecoveryTotal = 0UL;
unsigned long dailyRecoveryCount = 0UL;
unsigned long currentRecoverySeconds = 0UL;
bool recoveryActive = false;
unsigned long recoveryStartSeconds = 0UL;
unsigned long fastestRecoverySeconds = 0UL;
unsigned long slowestRecoverySeconds = 0UL;

// ================================================================
// DAILY TEMPERATURE EXTREMES  (NEW)
// ================================================================
float dailyMinTemperature = 999.0f;
float dailyMaxTemperature = -999.0f;

// ================================================================
// USER INTELLIGENCE
// ================================================================
int currentSimulationHour = 0;
int peakUsageHour = 0;
unsigned int peakUsageCount = 0;
int lastTrackedHour = -1;
unsigned int currentHourOpeningCount = 0;

// ================================================================
// ENERGY
// ================================================================
float temperatureEnergyImpact = 0.0f;
float doorEnergyImpact = 0.0f;
float recoveryEnergyImpact = 0.0f;
float energyWasteScore = 0.0f;
float energyEfficiencyScore = 100.0f;
float estimatedOperatingHours = 0.0f;
float estimatedDailyKWh = 0.0f;

// ================================================================
// HEALTH SCORE
// ================================================================
float temperatureScore = 100.0f;
float doorScore = 100.0f;
float recoveryScore = 100.0f;
float energyScore = 100.0f;
float healthScore = 100.0f;

// ================================================================
// PREDICTIVE MAINTENANCE
// ================================================================
float recoveryDay1 = 0.0f;
float recoveryDay2 = 0.0f;
float recoveryDay3 = 0.0f;
int validRecoveryDays = 0;
String predictiveMaintenanceStatus = "INSUFFICIENT HISTORY";
String predictiveMaintenanceMessage = "Need 3 valid recovery days";

// ================================================================
// STATUS
// ================================================================
String temperatureStatus = "NORMAL";
String doorStatus = "CLOSED";
String recoveryStatus = "STABLE";
String healthStatus = "EXCELLENT";
String energyStatus = "LOW";
String userBehavior = "NORMAL";
bool buzzerActive = false;

// ================================================================
// LIVE TEMPERATURE TREND RING BUFFER (NEW)
// 2s sampling x 90 samples = 3-minute rolling window
// ================================================================
#define TREND_BUFFER_SIZE 90
float trendBuffer[TREND_BUFFER_SIZE];
int trendIndex = 0;
bool trendFull = false;

void pushTrendSample(float value) {
  trendBuffer[trendIndex] = value;
  trendIndex = (trendIndex + 1) % TREND_BUFFER_SIZE;
  if (trendIndex == 0) trendFull = true;
}

// ================================================================
// LIVE ACTIVITY FEED (NEW)
// ================================================================
#define ACTIVITY_LOG_SIZE 8
struct ActivityEntry {
  unsigned long atSeconds;
  String message;
};
ActivityEntry activityLog[ACTIVITY_LOG_SIZE];
int activityHead = 0;
int activityCount = 0;

void addActivityEvent(const String &msg) {
  activityLog[activityHead].atSeconds = millis() / 1000UL;
  activityLog[activityHead].message = msg;
  activityHead = (activityHead + 1) % ACTIVITY_LOG_SIZE;
  if (activityCount < ACTIVITY_LOG_SIZE) activityCount++;
}

// ================================================================
// FUNCTION DECLARATIONS
// ================================================================
void loadPersistentData();
void savePersistentData();
void initializeHistoryFile();
void appendDailyHistory();
void trimHistoryFile();
int countHistoryRecords();
void updateDayCounter();
void readHardwareSensors();
void updateOledDisplay();
void updateDoorIntelligence(bool doorWasOpen);
void hourlyDoorOpeningEvent();
void updateTemperatureIntelligence();
void updateRecoveryIntelligence(bool doorWasOpen);
void calculateTemperatureScore();
void calculateDoorScore();
void calculateRecoveryScore();
void calculateEnergyModel();
void calculateHealthScore();
void calculateUserBehavior();
void calculatePredictiveMaintenance();
void updateSystemIntelligence();
void printSerialDashboard();
void handleRoot();
void handleData();
void handleTrend();
void handleDailyHistory();
void handleEvents();

// ================================================================
// SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================================");
  Serial.println(" SMART REFRIGERATOR HEALTH MONITORING SYSTEM");
  Serial.println(" FINAL SOFTWARE PROTOTYPE");
  Serial.println("================================================");

  pinMode(DOOR_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  if (oledReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Smart Refrigerator");
    display.println("Starting sensors...");
    display.display();
  } else {
    Serial.println("WARNING: OLED not found at I2C 0x3C.");
  }

  ds18b20.begin();
  ds18b20.setWaitForConversion(false);
  temperatureSensorReady = (ds18b20.getDeviceCount() > 0);
  ds18b20.requestTemperatures();
  lastTemperatureRequest = millis();
  dht.begin();
  delay(2000);
  float initialHumidity = dht.readHumidity();
  float initialDhtTemperature = dht.readTemperature();
  dhtSensorReady = !(isnan(initialHumidity) || isnan(initialDhtTemperature));
  if (dhtSensorReady) {
    humidity = initialHumidity;
    lastValidHumidityMillis = millis();
  }
  Serial.printf("DS18B20 detected: %s\n", temperatureSensorReady ? "YES" : "NO");
  Serial.printf("DHT22 reading valid: %s\n", dhtSensorReady ? "YES" : "NO");
  Serial.println("Door sensor configured on GPIO 17; buzzer on GPIO 25.");

  preferences.begin("refrigerator", false);
  loadPersistentData();

  if (!LittleFS.begin(true)) {
    Serial.println("ERROR: LittleFS initialization failed!");
  } else {
    Serial.println("LittleFS initialized successfully.");
    initializeHistoryFile();
  }

  WiFi.mode(WIFI_AP);
  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (apStarted) {
    Serial.println();
    Serial.println("WiFi SoftAP started.");
    Serial.print("SSID: "); Serial.println(AP_SSID);
    Serial.print("Password: "); Serial.println(AP_PASSWORD);
    Serial.print("Dashboard IP: "); Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("ERROR: SoftAP startup failed!");
  }

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/trend", handleTrend);
  server.on("/dailyhistory", handleDailyHistory);
  server.on("/events", handleEvents);
  server.begin();
  Serial.println("Web server started.");

  Serial.println();
  Serial.println("------------------------------------------------");
  Serial.println(" FINAL HEALTH SCORE WEIGHTS");
  Serial.println("------------------------------------------------");
  Serial.println("Temperature Stability : 40%");
  Serial.println("Door Behavior         : 20%");
  Serial.println("Cooling Recovery      : 20%");
  Serial.println("Energy Efficiency     : 20%");
  Serial.println("------------------------------------------------");
  Serial.println("REAL HARDWARE MODE: ENABLED");
  Serial.println("One software day = 24 hours.");
  Serial.println("------------------------------------------------");
  Serial.println();

  addActivityEvent("System started");

  previousMillis = millis();
  lastSimulationUpdate = millis();
  lastPreferenceSave = millis();
}

// ================================================================
// MAIN LOOP
// ================================================================
void loop() {
  server.handleClient();
  unsigned long currentMillis = millis();

  if (currentMillis - lastSimulationUpdate >= 1000UL) {
    unsigned long realDelta = currentMillis - lastSimulationUpdate;
    lastSimulationUpdate = currentMillis;

    bool doorWasOpen = doorOpen;

    simulatedElapsedMs += realDelta;
    dailyElapsedMs += realDelta;

    readHardwareSensors();

    updateDoorIntelligence(doorWasOpen);
    updateTemperatureIntelligence();
    updateRecoveryIntelligence(doorWasOpen);

    calculateTemperatureScore();
    calculateDoorScore();
    calculateRecoveryScore();
    calculateEnergyModel();
    calculateHealthScore();
    calculateUserBehavior();
    calculatePredictiveMaintenance();
    updateSystemIntelligence();

    digitalWrite(BUZZER_PIN, buzzerActive ? HIGH : LOW);

    if (currentMillis - lastOledUpdate >= OLED_INTERVAL_MS) {
      lastOledUpdate = currentMillis;
      updateOledDisplay();
    }

    if (currentMillis - previousMillis >= 5000UL) {
      previousMillis = currentMillis;
      printSerialDashboard();
    }
  }

  updateDayCounter();

  if (currentMillis - lastPreferenceSave >= 10000UL) {
    lastPreferenceSave = currentMillis;
    savePersistentData();
  }
}

// ================================================================
// LOAD / SAVE PERSISTENT DATA
// ================================================================
void loadPersistentData() {
  dayNumber = preferences.getInt("dayNumber", 1);
  dailyElapsedMs = preferences.getULong("dayElapsed", 0UL);
  recoveryDay1 = preferences.getFloat("recDay1", 0.0f);
  recoveryDay2 = preferences.getFloat("recDay2", 0.0f);
  recoveryDay3 = preferences.getFloat("recDay3", 0.0f);
  validRecoveryDays = preferences.getInt("validDays", 0);

  Serial.println();
  Serial.println("Persistent data loaded.");
  Serial.print("Current Software Day: "); Serial.println(dayNumber);
  Serial.print("Valid Recovery Days: "); Serial.println(validRecoveryDays);
}

void savePersistentData() {
  preferences.putInt("dayNumber", dayNumber);
  preferences.putULong("dayElapsed", dailyElapsedMs);
  preferences.putFloat("recDay1", recoveryDay1);
  preferences.putFloat("recDay2", recoveryDay2);
  preferences.putFloat("recDay3", recoveryDay3);
  preferences.putInt("validDays", validRecoveryDays);
}

// ================================================================
// HISTORY FILE HELPERS
// ================================================================
void initializeHistoryFile() {
  if (!LittleFS.exists(HISTORY_FILE)) {
    File file = LittleFS.open(HISTORY_FILE, "w");
    if (file) {
      file.println("[");
      file.println("]");
      file.close();
      Serial.println("New history file created.");
    } else {
      Serial.println("Unable to create history file.");
    }
  }
}

int countHistoryRecords() {
  if (!LittleFS.exists(HISTORY_FILE)) return 0;
  File file = LittleFS.open(HISTORY_FILE, "r");
  if (!file) return 0;
  String data = file.readString();
  file.close();
  int count = 0;
  int position = 0;
  while (true) {
    position = data.indexOf("\"day\"", position);
    if (position < 0) break;
    count++;
    position += 5;
  }
  return count;
}

void appendDailyHistory() {
  if (!LittleFS.exists(HISTORY_FILE)) initializeHistoryFile();

  File input = LittleFS.open(HISTORY_FILE, "r");
  if (!input) { Serial.println("Unable to read history file."); return; }
  String existingData = input.readString();
  input.close();

  int closingBracket = existingData.lastIndexOf("]");
  if (closingBracket < 0) { Serial.println("Invalid history file."); return; }
  existingData = existingData.substring(0, closingBracket);
  existingData.trim();
  bool hasPreviousRecord = existingData.length() > 1;

  bool hasRecoveryData = dailyRecoveryCount > 0UL;
  float averageRecovery = 0.0f;
  if (hasRecoveryData) averageRecovery = (float)dailyRecoveryTotal / (float)dailyRecoveryCount;

  float averageDoorDuration = 0.0f;
  if (dailyDoorOpenings > 0) averageDoorDuration = (float)dailyDoorOpenDurationSeconds / (float)dailyDoorOpenings;

  String record = "";
  record += "{";
  record += "\"day\":"; record += String(dayNumber);
  record += ",\"hasRecoveryData\":"; record += hasRecoveryData ? "true" : "false";
  record += ",\"avgRecoverySeconds\":";
  record += hasRecoveryData ? String(averageRecovery, 1) : String("null");
  record += ",\"recoveryCount\":"; record += String(dailyRecoveryCount);
  record += ",\"doorOpenings\":"; record += String(dailyDoorOpenings);
  record += ",\"avgDoorSeconds\":"; record += String(averageDoorDuration, 1);
  record += ",\"estimatedKWh\":"; record += String(estimatedDailyKWh, 3);
  record += ",\"healthScore\":"; record += String(healthScore, 1);
  record += ",\"minTemp\":"; record += String(dailyMinTemperature <= 900.0f ? dailyMinTemperature : refrigeratorTemperature, 1);
  record += ",\"maxTemp\":"; record += String(dailyMaxTemperature >= -900.0f ? dailyMaxTemperature : refrigeratorTemperature, 1);
  record += "}";

  File output = LittleFS.open(HISTORY_FILE, "w");
  if (!output) { Serial.println("Unable to write history file."); return; }
  output.print(existingData);
  if (hasPreviousRecord) output.println(",");
  output.println(record);
  output.println("]");
  output.close();

  Serial.print("History saved for Day "); Serial.println(dayNumber);

  trimHistoryFile();
}

void trimHistoryFile() {
  int recordCount = countHistoryRecords();
  if (recordCount <= MAX_HISTORY_RECORDS) return;

  File input = LittleFS.open(HISTORY_FILE, "r");
  if (!input) return;
  String data = input.readString();
  input.close();

  String records[MAX_HISTORY_RECORDS + 10];
  int found = 0;
  int searchPosition = 0;
  while (true) {
    int start = data.indexOf("{", searchPosition);
    if (start < 0) break;
    int end = data.indexOf("}", start);
    if (end < 0) break;
    if (found < MAX_HISTORY_RECORDS + 10) {
      records[found] = data.substring(start, end + 1);
      found++;
    }
    searchPosition = end + 1;
  }
  if (found <= MAX_HISTORY_RECORDS) return;

  int firstRecord = found - MAX_HISTORY_RECORDS;
  File output = LittleFS.open(HISTORY_FILE, "w");
  if (!output) return;
  output.println("[");
  for (int i = firstRecord; i < found; i++) {
    output.print(records[i]);
    output.println(i < found - 1 ? "," : "");
  }
  output.println("]");
  output.close();

  Serial.print("History trimmed to "); Serial.print(MAX_HISTORY_RECORDS); Serial.println(" records.");
}

// ================================================================
// SOFTWARE DAY COUNTER
// ================================================================
void updateDayCounter() {
  unsigned long dayDuration = DEMO_MODE ? DEMO_DAY_DURATION_MS : REAL_DAY_DURATION_MS;

  if (dailyElapsedMs >= dayDuration) {
    appendDailyHistory();

    if (dailyRecoveryCount > 0UL) {
      float completedAverage = (float)dailyRecoveryTotal / (float)dailyRecoveryCount;
      recoveryDay1 = recoveryDay2;
      recoveryDay2 = recoveryDay3;
      recoveryDay3 = completedAverage;
      if (validRecoveryDays < 3) validRecoveryDays++;
      Serial.print("Valid recovery day stored: "); Serial.println(completedAverage);
    } else {
      Serial.println("Completed day has NO recovery data.");
      Serial.println("Day skipped for prediction trend.");
    }

    addActivityEvent("Day " + String(dayNumber) + " complete, health " + String(healthScore, 0) + "%");

    dayNumber++;
    dailyElapsedMs = 0UL;

    dailyDoorOpenings = 0;
    dailyDoorOpenDurationSeconds = 0UL;
    dailyLongDoorOpenings = 0;
    dailyLongestDoorOpenSeconds = 0UL;
    dailyRecoveryTotal = 0UL;
    dailyRecoveryCount = 0UL;
    currentDoorOpenSeconds = 0UL;
    currentRecoverySeconds = 0UL;
    recoveryActive = false;
    peakUsageHour = 0;
    peakUsageCount = 0;
    currentHourOpeningCount = 0;
    lastTrackedHour = -1;
    estimatedOperatingHours = 0.0f;
    estimatedDailyKWh = 0.0f;
    recoveryStatus = "STABLE";
    dailyMinTemperature = 999.0f;
    dailyMaxTemperature = -999.0f;

    savePersistentData();

    Serial.println();
    Serial.println("================================================");
    Serial.println(" NEW SOFTWARE DAY");
    Serial.println("================================================");
    Serial.print("Current Day: "); Serial.println(dayNumber);
    Serial.print("Valid recovery days: "); Serial.println(validRecoveryDays);
    Serial.println("Daily health statistics reset.");
    Serial.println("================================================");
    Serial.println();
  }
}

// ================================================================
// SENSOR READING
// ================================================================
void readHardwareSensors() {
  unsigned long now = millis();

  if (now - lastTemperatureRequest >= SENSOR_INTERVAL_MS) {
    float value = ds18b20.getTempCByIndex(0);
    if (temperatureSensorReady && value != DEVICE_DISCONNECTED_C &&
        value > -40.0f && value < 85.0f) {
      refrigeratorTemperature = value;
      temperatureReadingValid = true;
      lastValidTemperatureMillis = now;

      pushTrendSample(refrigeratorTemperature);
      if (refrigeratorTemperature < dailyMinTemperature) dailyMinTemperature = refrigeratorTemperature;
      if (refrigeratorTemperature > dailyMaxTemperature) dailyMaxTemperature = refrigeratorTemperature;
    } else {
      temperatureReadingValid = false;
    }
    ds18b20.requestTemperatures();
    lastTemperatureRequest = now;
  }

  if (now - lastDhtRead >= SENSOR_INTERVAL_MS) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    lastDhtRead = now;
    if (!isnan(h) && !isnan(t) && h >= 0.0f && h <= 100.0f && t > -40.0f && t < 80.0f) {
      humidity = h;
      humidityReadingValid = true;
      lastValidHumidityMillis = now;
      dhtSensorReady = true;
    } else {
      humidityReadingValid = false;
    }
  }

  // Typical MC-38 NC contact: circuit closed/magnet near = LOW;
  // circuit open/magnet away = HIGH. Reverse if your unit behaves oppositely.
  doorOpen = (digitalRead(DOOR_PIN) == HIGH);

  if (now - lastValidTemperatureMillis > SENSOR_STALE_MS) temperatureReadingValid = false;
  if (now - lastValidHumidityMillis > SENSOR_STALE_MS) humidityReadingValid = false;
}

void updateOledDisplay() {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SMART REFRIGERATOR");
  display.print("Temp: ");
  if (temperatureReadingValid) { display.print(refrigeratorTemperature, 1); display.println(" C"); }
  else display.println("Sensor error");
  display.print("Humidity: ");
  if (humidityReadingValid) { display.print(humidity, 0); display.println(" %"); }
  else display.println("Sensor error");
  display.print("Door: "); display.println(doorOpen ? "OPEN" : "CLOSED");
  display.print("Health: "); display.print(healthScore, 0); display.println("/100");
  display.print("Est kWh/day: "); display.println(estimatedDailyKWh, 2);
  display.print("Day: "); display.println(dayNumber);
  display.display();
}

// ================================================================
// DOOR INTELLIGENCE
// ================================================================
void updateDoorIntelligence(bool doorWasOpen) {
  if (doorOpen && !doorWasOpen) {
    dailyDoorOpenings++;
    currentDoorOpenSeconds = 0UL;

    unsigned long totalSeconds = simulatedElapsedMs / 1000UL;
    currentSimulationHour = (int)((totalSeconds / 3600UL) % 24UL);
    hourlyDoorOpeningEvent();

    addActivityEvent("Door opened");
    Serial.println("EVENT: Refrigerator door OPENED");
  }

  if (doorOpen) {
    currentDoorOpenSeconds++;
    buzzerActive = (currentDoorOpenSeconds >= DOOR_WARNING_SECONDS);
  }

  if (!doorOpen && doorWasOpen) {
    dailyDoorOpenDurationSeconds += currentDoorOpenSeconds;
    if (currentDoorOpenSeconds > dailyLongestDoorOpenSeconds) {
      dailyLongestDoorOpenSeconds = currentDoorOpenSeconds;
    }
    if (currentDoorOpenSeconds >= DOOR_WARNING_SECONDS) dailyLongDoorOpenings++;

    buzzerActive = false;

    addActivityEvent("Door closed after " + String(currentDoorOpenSeconds) + "s");
    Serial.print("Door closed after "); Serial.print(currentDoorOpenSeconds); Serial.println(" seconds.");

    currentDoorOpenSeconds = 0UL;
  }
}

void hourlyDoorOpeningEvent() {
  if (lastTrackedHour == -1) {
    lastTrackedHour = currentSimulationHour;
    currentHourOpeningCount = 0;
  }
  if (currentSimulationHour != lastTrackedHour) {
    currentHourOpeningCount = 0;
    lastTrackedHour = currentSimulationHour;
  }
  currentHourOpeningCount++;
  if (currentHourOpeningCount > peakUsageCount) {
    peakUsageCount = currentHourOpeningCount;
    peakUsageHour = currentSimulationHour;
  }
}

// ================================================================
// TEMPERATURE INTELLIGENCE
// ================================================================
void updateTemperatureIntelligence() {
  if (refrigeratorTemperature <= TARGET_TEMPERATURE + 1.0f) temperatureStatus = "NORMAL";
  else if (refrigeratorTemperature <= TEMP_WARNING_LIMIT) temperatureStatus = "WARNING";
  else if (refrigeratorTemperature <= TEMP_HIGH_LIMIT) temperatureStatus = "HIGH";
  else temperatureStatus = "CRITICAL";
}

// ================================================================
// COOLING RECOVERY
// ================================================================
void updateRecoveryIntelligence(bool doorWasOpen) {
  if (!doorOpen && doorWasOpen && refrigeratorTemperature > RECOVERY_TEMPERATURE) {
    recoveryActive = true;
    recoveryStartSeconds = simulatedElapsedMs / 1000UL;
    currentRecoverySeconds = 0UL;
    recoveryStatus = "RECOVERING";
    addActivityEvent("Cooling recovery started");
    Serial.println("EVENT: Cooling recovery started.");
  }

  if (recoveryActive) {
    unsigned long currentSeconds = simulatedElapsedMs / 1000UL;
    if (currentSeconds >= recoveryStartSeconds) {
      currentRecoverySeconds = currentSeconds - recoveryStartSeconds;
    }

    if (refrigeratorTemperature <= RECOVERY_TEMPERATURE) {
      recoveryActive = false;
      dailyRecoveryCount++;
      dailyRecoveryTotal += currentRecoverySeconds;

      if (currentRecoverySeconds < fastestRecoverySeconds || fastestRecoverySeconds == 0UL) {
        fastestRecoverySeconds = currentRecoverySeconds;
      }
      if (currentRecoverySeconds > slowestRecoverySeconds) {
        slowestRecoverySeconds = currentRecoverySeconds;
      }

      if (currentRecoverySeconds <= GOOD_RECOVERY_SECONDS) recoveryStatus = "GOOD";
      else if (currentRecoverySeconds <= MODERATE_RECOVERY_SECONDS) recoveryStatus = "MODERATE";
      else recoveryStatus = "SLOW";

      addActivityEvent("Recovery completed in " + String(currentRecoverySeconds) + "s");
      Serial.print("EVENT: Cooling recovery completed in ");
      Serial.print(currentRecoverySeconds);
      Serial.println(" seconds.");
    }
  }

  if (!recoveryActive && refrigeratorTemperature <= RECOVERY_TEMPERATURE) {
    if (recoveryStatus != "GOOD" && recoveryStatus != "MODERATE" && recoveryStatus != "SLOW") {
      recoveryStatus = "STABLE";
    }
  }
}

// ================================================================
// SCORES
// ================================================================
void calculateTemperatureScore() {
  if (refrigeratorTemperature >= 3.0f && refrigeratorTemperature <= 5.0f) temperatureScore = 100.0f;
  else if (refrigeratorTemperature <= 6.0f) temperatureScore = 85.0f;
  else if (refrigeratorTemperature <= 7.0f) temperatureScore = 65.0f;
  else if (refrigeratorTemperature <= 8.0f) temperatureScore = 40.0f;
  else temperatureScore = 20.0f;
}

void calculateDoorScore() {
  if (dailyDoorOpenings == 0) { doorScore = 100.0f; return; }
  float longOpeningPercentage = ((float)dailyLongDoorOpenings / (float)dailyDoorOpenings) * 100.0f;
  if (longOpeningPercentage <= 10.0f) doorScore = 100.0f;
  else if (longOpeningPercentage <= 25.0f) doorScore = 85.0f;
  else if (longOpeningPercentage <= 50.0f) doorScore = 65.0f;
  else doorScore = 40.0f;
}

void calculateRecoveryScore() {
  if (dailyRecoveryCount == 0UL) { recoveryScore = 100.0f; return; }
  float averageRecovery = (float)dailyRecoveryTotal / (float)dailyRecoveryCount;
  if (averageRecovery <= (float)GOOD_RECOVERY_SECONDS) recoveryScore = 100.0f;
  else if (averageRecovery <= (float)MODERATE_RECOVERY_SECONDS) recoveryScore = 75.0f;
  else if (averageRecovery <= 360.0f) recoveryScore = 50.0f;
  else recoveryScore = 25.0f;
}

void calculateEnergyModel() {
  if (refrigeratorTemperature <= 5.0f) temperatureEnergyImpact = 0.0f;
  else if (refrigeratorTemperature <= 6.0f) temperatureEnergyImpact = 15.0f;
  else if (refrigeratorTemperature <= 7.0f) temperatureEnergyImpact = 30.0f;
  else temperatureEnergyImpact = 50.0f;

  if (dailyDoorOpenings == 0) {
    doorEnergyImpact = 0.0f;
  } else {
    float longPercentage = ((float)dailyLongDoorOpenings / (float)dailyDoorOpenings) * 100.0f;
    doorEnergyImpact = (longPercentage > 50.0f) ? 50.0f : longPercentage;
  }

  if (dailyRecoveryCount == 0UL) {
    recoveryEnergyImpact = 0.0f;
  } else {
    float averageRecovery = (float)dailyRecoveryTotal / (float)dailyRecoveryCount;
    if (averageRecovery <= 120.0f) recoveryEnergyImpact = 0.0f;
    else if (averageRecovery <= 240.0f) recoveryEnergyImpact = 15.0f;
    else if (averageRecovery <= 360.0f) recoveryEnergyImpact = 30.0f;
    else recoveryEnergyImpact = 50.0f;
  }

  energyWasteScore = (temperatureEnergyImpact * 0.40f) + (doorEnergyImpact * 0.30f) + (recoveryEnergyImpact * 0.30f);
  if (energyWasteScore > 100.0f) energyWasteScore = 100.0f;
  energyEfficiencyScore = 100.0f - energyWasteScore;

  if (energyWasteScore <= 20.0f) energyStatus = "LOW";
  else if (energyWasteScore <= 40.0f) energyStatus = "MODERATE";
  else if (energyWasteScore <= 60.0f) energyStatus = "HIGH";
  else energyStatus = "VERY HIGH";

  float modeledDutyCycle = BASE_DUTY_CYCLE;
  if (refrigeratorTemperature > 5.0f) modeledDutyCycle += 0.10f;
  if (refrigeratorTemperature > 6.0f) modeledDutyCycle += 0.10f;

  float doorDuty = doorEnergyImpact / 1000.0f;
  float recoveryDuty = recoveryEnergyImpact / 1000.0f;
  modeledDutyCycle += doorDuty + recoveryDuty;
  if (modeledDutyCycle > 1.0f) modeledDutyCycle = 1.0f;

  float currentDayHours = (float)dailyElapsedMs / 3600000.0f;
  estimatedOperatingHours = currentDayHours * modeledDutyCycle;
  estimatedDailyKWh = (REFERENCE_POWER_W * estimatedOperatingHours) / 1000.0f;
}

void calculateHealthScore() {
  energyScore = energyEfficiencyScore;
  healthScore = (temperatureScore * 0.40f) + (doorScore * 0.20f) + (recoveryScore * 0.20f) + (energyScore * 0.20f);

  if (healthScore >= 85.0f) healthStatus = "EXCELLENT";
  else if (healthScore >= 70.0f) healthStatus = "GOOD";
  else if (healthScore >= 50.0f) healthStatus = "MODERATE";
  else healthStatus = "POOR";
}

void calculateUserBehavior() {
  if (dailyDoorOpenings == 0) { userBehavior = "NORMAL"; return; }
  float averageOpeningDuration = (float)dailyDoorOpenDurationSeconds / (float)dailyDoorOpenings;
  float longOpeningPercentage = ((float)dailyLongDoorOpenings / (float)dailyDoorOpenings) * 100.0f;

  if (averageOpeningDuration <= 20.0f && longOpeningPercentage <= 20.0f) userBehavior = "NORMAL";
  else if (averageOpeningDuration <= 40.0f && longOpeningPercentage <= 50.0f) userBehavior = "MODERATE";
  else userBehavior = "HIGH";
}

void calculatePredictiveMaintenance() {
  if (validRecoveryDays < 3) {
    predictiveMaintenanceStatus = "INSUFFICIENT HISTORY";
    predictiveMaintenanceMessage = "Need 3 valid recovery days";
    return;
  }

  bool increasingTrend = (recoveryDay1 < recoveryDay2) && (recoveryDay2 < recoveryDay3);
  if (increasingTrend) {
    float increase = recoveryDay3 - recoveryDay1;
    float base = recoveryDay1;
    if (base > 0.0f) {
      float percentage = (increase / base) * 100.0f;
      if (percentage >= 15.0f) {
        if (predictiveMaintenanceStatus != "MAINTENANCE ADVISED") {
          addActivityEvent("Maintenance advised: recovery trend rising");
        }
        predictiveMaintenanceStatus = "MAINTENANCE ADVISED";
        predictiveMaintenanceMessage = "Recovery time is increasing";
        return;
      }
    }
  }

  predictiveMaintenanceStatus = "NORMAL";
  predictiveMaintenanceMessage = "Cooling recovery trend is stable";
}

void updateSystemIntelligence() {
  if (doorOpen) {
    if (currentDoorOpenSeconds >= DOOR_ALERT_SECONDS) doorStatus = "ALERT";
    else if (currentDoorOpenSeconds >= DOOR_WARNING_SECONDS) doorStatus = "WARNING";
    else doorStatus = "OPEN";
  } else {
    doorStatus = "CLOSED";
  }

  if (recoveryActive) recoveryStatus = "RECOVERING";
}

// ================================================================
// SERIAL DASHBOARD
// ================================================================
void printSerialDashboard() {
  Serial.println();
  Serial.println("================================================");
  Serial.println(" SMART REFRIGERATOR STATUS");
  Serial.println("================================================");
  Serial.print("Software Day          : "); Serial.println(dayNumber);
  Serial.print("Temperature           : "); Serial.print(refrigeratorTemperature, 2); Serial.println(" C");
  Serial.print("Humidity              : "); Serial.print(humidity, 1); Serial.println(" %");
  Serial.print("Daily Min/Max Temp    : "); Serial.print(dailyMinTemperature, 1); Serial.print(" / "); Serial.println(dailyMaxTemperature, 1);
  Serial.print("Temperature Status    : "); Serial.println(temperatureStatus);
  Serial.print("Door Status           : "); Serial.println(doorStatus);
  Serial.print("Daily Door Openings   : "); Serial.println(dailyDoorOpenings);
  Serial.print("Peak Usage Hour       : "); Serial.print(peakUsageHour); Serial.print(":00 ("); Serial.print(peakUsageCount); Serial.println(" openings)");
  Serial.print("User Behavior         : "); Serial.println(userBehavior);
  Serial.print("Recovery Status       : "); Serial.println(recoveryStatus);
  Serial.print("Today's Recovery Count: "); Serial.println(dailyRecoveryCount);
  Serial.println("------------------------------------------------");
  Serial.print("HEALTH SCORE          : "); Serial.print(healthScore, 1); Serial.print(" / 100 - "); Serial.println(healthStatus);
  Serial.print("Estimated Energy      : "); Serial.print(estimatedDailyKWh, 3); Serial.println(" kWh");
  Serial.print("Predictive Maintenance: "); Serial.println(predictiveMaintenanceStatus);
  Serial.print("Uptime (s)            : "); Serial.println(millis() / 1000UL);
  Serial.println("================================================");
}

// ================================================================
// NEW JSON ENDPOINTS: /trend  /dailyhistory  /events
// ================================================================
void handleTrend() {
  String json = "[";
  int count = trendFull ? TREND_BUFFER_SIZE : trendIndex;
  int startIdx = trendFull ? trendIndex : 0;
  for (int i = 0; i < count; i++) {
    int idx = (startIdx + i) % TREND_BUFFER_SIZE;
    json += String(trendBuffer[idx], 2);
    if (i < count - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleDailyHistory() {
  if (!LittleFS.exists(HISTORY_FILE)) {
    server.send(200, "application/json", "[]");
    return;
  }
  File file = LittleFS.open(HISTORY_FILE, "r");
  if (!file) {
    server.send(200, "application/json", "[]");
    return;
  }
  String data = file.readString();
  file.close();
  server.send(200, "application/json", data);
}

void handleEvents() {
  String json = "[";
  int newestIdx = (activityHead - 1 + ACTIVITY_LOG_SIZE) % ACTIVITY_LOG_SIZE;
  for (int i = 0; i < activityCount; i++) {
    int idx = (newestIdx - i + ACTIVITY_LOG_SIZE) % ACTIVITY_LOG_SIZE;
    json += "{\"t\":" + String(activityLog[idx].atSeconds) + ",\"m\":\"" + activityLog[idx].message + "\"}";
    if (i < activityCount - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ================================================================
// LIVE DATA ENDPOINT
// ================================================================
void handleData() {
  String json = "{";
  json += "\"day\":" + String(dayNumber);
  json += ",\"temperature\":" + String(refrigeratorTemperature, 2);
  json += ",\"humidity\":" + String(humidity, 1);
  json += ",\"temperatureValid\":"; json += temperatureReadingValid ? "true" : "false";
  json += ",\"humidityValid\":"; json += humidityReadingValid ? "true" : "false";
  json += ",\"doorOpen\":"; json += doorOpen ? "true" : "false";
  json += ",\"doorOpenings\":" + String(dailyDoorOpenings);
  json += ",\"healthScore\":" + String(healthScore, 1);
  json += ",\"temperatureScore\":" + String(temperatureScore, 1);
  json += ",\"doorScore\":" + String(doorScore, 1);
  json += ",\"recoveryScore\":" + String(recoveryScore, 1);
  json += ",\"energyScore\":" + String(energyScore, 1);
  json += ",\"estimatedKWh\":" + String(estimatedDailyKWh, 3);
  json += ",\"energyEfficiency\":" + String(energyEfficiencyScore, 1);
  json += ",\"recoveryCount\":" + String(dailyRecoveryCount);
  json += ",\"peakUsageHour\":" + String(peakUsageHour);
  json += ",\"predictiveMaintenance\":\"" + predictiveMaintenanceStatus + "\"";
  json += ",\"predictiveMaintenanceMessage\":\"" + predictiveMaintenanceMessage + "\"";
  json += ",\"recoveryStatus\":\"" + recoveryStatus + "\"";
  json += ",\"userBehavior\":\"" + userBehavior + "\"";
  json += ",\"validRecoveryDays\":" + String(validRecoveryDays);
  json += ",\"doorStatus\":\"" + doorStatus + "\"";
  json += ",\"minTemp\":" + String(dailyMinTemperature <= 900.0f ? dailyMinTemperature : refrigeratorTemperature, 1);
  json += ",\"maxTemp\":" + String(dailyMaxTemperature >= -900.0f ? dailyMaxTemperature : refrigeratorTemperature, 1);
  json += ",\"uptimeSeconds\":" + String(millis() / 1000UL);
  json += "}";
  server.send(200, "application/json", json);
}

// ================================================================
// PREMIUM WEB DASHBOARD  (self-contained: no CDN, no external fonts)
// ================================================================
void handleRoot() {
  String html = R"HTML(<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#060b14"><title>Smart Refrigerator | Live Monitor</title>
<style>
:root{--bg:#070c16;--panel:#0f1727;--panel2:#131f33;--line:#1f3049;--text:#f3f7ff;--muted:#8ea0bd;--cyan:#4fe0ea;--violet:#9c7bff;--green:#49dba0;--amber:#ffc266;--red:#ff6b7f;--radius:22px}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.55 ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;overflow-x:hidden}
.bgfx{position:fixed;inset:0;z-index:-1;overflow:hidden}
.blob{position:absolute;border-radius:50%;filter:blur(90px);opacity:.35}
.b1{width:480px;height:480px;background:radial-gradient(circle,#2b6bff,transparent 70%);top:-180px;left:-120px;animation:float1 16s ease-in-out infinite}
.b2{width:420px;height:420px;background:radial-gradient(circle,#19d3c5,transparent 70%);bottom:-160px;right:-100px;animation:float2 19s ease-in-out infinite}
.b3{width:340px;height:340px;background:radial-gradient(circle,#8a5cff,transparent 70%);top:40%;left:50%;animation:float3 22s ease-in-out infinite}
@keyframes float1{0%,100%{transform:translate(0,0)}50%{transform:translate(60px,40px)}}
@keyframes float2{0%,100%{transform:translate(0,0)}50%{transform:translate(-50px,-30px)}}
@keyframes float3{0%,100%{transform:translate(-50%,-50%) scale(1)}50%{transform:translate(-50%,-50%) scale(1.15)}}
.wrap{max-width:1480px;margin:auto;padding:26px}
.alertbar{position:fixed;top:0;left:0;right:0;z-index:50;transform:translateY(-100%);transition:transform .4s cubic-bezier(.2,.9,.3,1.2);
  background:linear-gradient(90deg,#3d1420,#2a1a0f);border-bottom:1px solid #4a2230;color:#ffd9d9;padding:12px 20px;text-align:center;font-size:13px;font-weight:600}
.alertbar.show{transform:translateY(0)}
.topbar{display:flex;align-items:center;justify-content:space-between;gap:18px;margin-bottom:22px;flex-wrap:wrap}
.brand{display:flex;align-items:center;gap:14px}
.logo{width:50px;height:50px;border-radius:16px;background:linear-gradient(135deg,var(--cyan),var(--violet));display:grid;place-items:center;color:#06111f;font-size:26px;font-weight:900;box-shadow:0 10px 30px #2b6bff33}
.eyebrow{color:var(--cyan);text-transform:uppercase;letter-spacing:.18em;font-size:11px;font-weight:800}
.brand h1{font-size:clamp(22px,3vw,32px);line-height:1.15;margin:3px 0;background:linear-gradient(90deg,#fff,#b8d4ff);-webkit-background-clip:text;background-clip:text;color:transparent}
.sub{color:var(--muted);font-size:13px}
.live{display:flex;align-items:center;gap:9px;padding:10px 14px;border:1px solid var(--line);border-radius:999px;background:#0e1826;color:#c7d7eb;font-size:12px;white-space:nowrap}
.dot{width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 0 4px #49dba01c;animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
.hero{display:grid;grid-template-columns:1.3fr .7fr;gap:18px;margin-bottom:18px}
.glass{background:linear-gradient(160deg,rgba(255,255,255,.04),rgba(255,255,255,.01)),var(--panel);border:1px solid var(--line);border-radius:var(--radius);box-shadow:0 20px 50px #00000030;backdrop-filter:blur(6px)}
.hero-main{padding:30px;position:relative;overflow:hidden}
.hero-main h2{font-size:clamp(24px,3.4vw,36px);margin:10px 0;letter-spacing:-.02em}
.hero-main p{color:var(--muted);max-width:620px;margin:0}
.hero-tags{display:flex;flex-wrap:wrap;gap:8px;margin-top:22px}
.tag{padding:7px 12px;border-radius:9px;background:#152337;border:1px solid #28405e;color:#bfd0e7;font-size:12px}
.hero-ring{padding:26px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:10px}
.ringwrap{position:relative;width:188px;height:188px}
.ringwrap svg{transform:rotate(-90deg)}
.ringtrack{fill:none;stroke:#1c2c43;stroke-width:14}
.ringval{fill:none;stroke:url(#ringGrad);stroke-width:14;stroke-linecap:round;transition:stroke-dashoffset 1s cubic-bezier(.2,.8,.2,1)}
.ringcenter{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center}
.ringcenter .num{font-size:40px;font-weight:800;letter-spacing:-.03em}
.ringcenter .lbl{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.1em;margin-top:2px}
.ringstatus{font-size:13px;font-weight:700;color:var(--green)}
.section-label{display:flex;justify-content:space-between;align-items:end;gap:12px;margin:26px 2px 12px;flex-wrap:wrap}
.section-label h2{font-size:17px;margin:0}
.section-label p{margin:3px 0 0;color:var(--muted);font-size:12px}
.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:14px}
.card{padding:19px;min-width:0;transition:transform .18s,border-color .18s}
.card:hover{transform:translateY(-3px);border-color:#3c5879}
.card-top{display:flex;justify-content:space-between;align-items:center;gap:8px}
.card-title{color:var(--muted);font-size:12.5px;font-weight:600}
.icon{width:36px;height:36px;border-radius:11px;display:grid;place-items:center;background:#17293f;color:var(--cyan);font-size:17px}
.metric{font-size:clamp(22px,2.6vw,30px);font-weight:780;letter-spacing:-.03em;margin:11px 0 2px}
.unit{font-size:14px;color:#a9bdd7}
.meta{font-size:12px;color:var(--muted)}
.pill{display:inline-flex;align-items:center;gap:6px;padding:5px 9px;border-radius:99px;background:#16352f;color:var(--green);font-size:11px;font-weight:800;margin-top:12px}
.pill.warn{background:#3a2c1b;color:var(--amber)}
.pill.bad{background:#3d202b;color:var(--red)}
.twocol{display:grid;grid-template-columns:1.1fr .9fr;gap:14px;margin-top:14px}
.section{padding:22px}
.section h3{margin:0 0 5px;font-size:17px}
.section-intro{margin:0 0 18px;color:var(--muted);font-size:12px}
.score-row{display:grid;grid-template-columns:1fr auto;gap:8px;margin:13px 0 4px;font-size:13px}
.bar{height:8px;background:#1b2b42;border-radius:99px;overflow:hidden}
.bar span{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--cyan),var(--green));border-radius:99px;transition:width .6s}
canvas{width:100%;display:block;border-radius:14px}
.sparkwrap{position:relative;height:160px;background:#0c1423;border-radius:14px;border:1px solid var(--line);overflow:hidden}
.daybars{display:flex;align-items:flex-end;gap:6px;height:120px;margin-top:6px}
.daybar{flex:1;background:linear-gradient(180deg,var(--cyan),#1b5a63);border-radius:5px 5px 2px 2px;min-height:4px;position:relative;transition:height .5s}
.daybar .lbl{position:absolute;bottom:-20px;left:0;right:0;text-align:center;font-size:10px;color:var(--muted)}
.feed{display:flex;flex-direction:column;gap:0;max-height:260px;overflow-y:auto}
.feeditem{display:flex;gap:10px;padding:11px 0;border-bottom:1px solid #1c2c43;font-size:13px}
.feeditem:last-child{border-bottom:0}
.feeddot{width:7px;height:7px;border-radius:50%;background:var(--cyan);margin-top:6px;flex-shrink:0}
.feedtext{color:#dbe6f5}
.feedtime{color:var(--muted);font-size:11px;white-space:nowrap}
.statrow{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px}
.stat{background:#0d1726;border:1px solid var(--line);border-radius:12px;padding:12px}
.stat .k{font-size:11px;color:var(--muted)}
.stat .v{font-size:19px;font-weight:700;margin-top:3px}
.notice{padding:13px 15px;border:1px solid #594629;background:#2a241b;border-radius:13px;color:#ffd69a;font-size:12.5px;margin-top:14px}
.footer{display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;color:#64789a;font-size:11px;padding:24px 3px 4px}
.mono{font-variant-numeric:tabular-nums}
@media(max-width:1000px){.hero{grid-template-columns:1fr}.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.twocol{grid-template-columns:1fr}}
@media(max-width:560px){.wrap{padding:14px}.grid{grid-template-columns:1fr 1fr;gap:9px}.card{padding:14px;border-radius:15px}.metric{font-size:22px}}
</style></head><body>
<div class="bgfx"><div class="blob b1"></div><div class="blob b2"></div><div class="blob b3"></div></div>
<div class="alertbar" id="alertBar">⚠ Attention needed</div>
<main class="wrap">
<header class="topbar"><div class="brand"><div class="logo">❄</div><div><div class="eyebrow">Edge monitoring · ESP32</div><h1>Smart Refrigerator</h1><div class="sub">Health monitoring &amp; energy optimization</div></div></div><div class="live"><span class="dot" id="liveDot"></span><span id="connectionText">LOCAL DEVICE · CONNECTED</span></div></header>

<section class="hero">
<div class="glass hero-main"><div class="eyebrow">Refrigeration intelligence</div><h2>Know your refrigerator.<br>Before it becomes a problem.</h2><p>Live environmental readings, door-use patterns, cooling recovery analysis and an on-device health assessment — running entirely on your local WiFi, no cloud required.</p>
<div class="hero-tags"><span class="tag">● Local-only dashboard</span><span class="tag">DS18B20 + DHT22</span><span class="tag">Door event monitoring</span><span class="tag" id="uptimeTag">Uptime —</span></div>
<div class="statrow">
<div class="stat"><div class="k">Daily min / max</div><div class="v" id="minMaxTemp">— / — °C</div></div>
<div class="stat"><div class="k">Software day</div><div class="v" id="dayStat">—</div></div>
</div>
</div>
<div class="glass hero-ring"><div class="ringwrap">
<svg width="188" height="188" viewBox="0 0 188 188">
<defs><linearGradient id="ringGrad" x1="0%" y1="0%" x2="100%" y2="100%"><stop offset="0%" stop-color="#4fe0ea"/><stop offset="100%" stop-color="#49dba0"/></linearGradient></defs>
<circle class="ringtrack" cx="94" cy="94" r="80"/>
<circle class="ringval" id="ringVal" cx="94" cy="94" r="80" stroke-dasharray="502.65" stroke-dashoffset="502.65"/>
</svg>
<div class="ringcenter"><div class="num mono" id="healthBig">--</div><div class="lbl">Health score</div></div>
</div><div class="ringstatus" id="healthStatus">Waiting for data</div></div>
</section>

<div class="section-label"><div><h2>Live telemetry</h2><p>Current readings and operating indicators</p></div><div class="sub" id="lastUpdated">Connecting…</div></div>
<section class="grid">
<article class="glass card"><div class="card-top"><span class="card-title">Cabinet temperature</span><span class="icon">🌡</span></div><div class="metric mono" id="temperatureValue">— <span class="unit">°C</span></div><div class="meta" id="temperatureMeta">Waiting for sensor</div><span class="pill" id="temperaturePill">CHECKING</span></article>
<article class="glass card"><div class="card-top"><span class="card-title">Relative humidity</span><span class="icon">◌</span></div><div class="metric mono" id="humidityValue">— <span class="unit">%</span></div><div class="meta" id="humidityMeta">Environment sensor</div><span class="pill" id="humidityPill">CHECKING</span></article>
<article class="glass card"><div class="card-top"><span class="card-title">Door status</span><span class="icon">▣</span></div><div class="metric" id="doorValue">—</div><div class="meta">Daily openings: <strong id="doorOpenings">—</strong></div><span class="pill" id="doorPill">CHECKING</span></article>
<article class="glass card"><div class="card-top"><span class="card-title">Cooling recovery</span><span class="icon">↻</span></div><div class="metric" id="recoveryValue" style="font-size:22px">—</div><div class="meta">Recovery events today: <strong id="recoveryCount">—</strong></div><span class="pill" id="recoveryPill">MONITORING</span></article>
</section>

<div class="twocol">
<article class="glass section"><h3>Temperature trend</h3><p class="section-intro">Last 3 minutes, live sensor readings</p>
<div class="sparkwrap"><canvas id="sparkCanvas"></canvas></div>
</article>
<article class="glass section"><h3>Live activity feed</h3><p class="section-intro">Door, recovery and system events</p>
<div class="feed" id="feed"><div class="feeditem"><span class="feeddot"></span><span class="feedtext">Waiting for events…</span></div></div>
</article>
</div>

<div class="twocol">
<article class="glass section"><h3>Health score breakdown</h3><p class="section-intro">Weighted components · total out of 100</p>
<div class="score-row"><span>Temperature stability <span class="meta">· 40%</span></span><strong id="tempScoreText">—</strong></div><div class="bar"><span id="tempScoreBar"></span></div>
<div class="score-row"><span>Door behavior <span class="meta">· 20%</span></span><strong id="doorScoreText">—</strong></div><div class="bar"><span id="doorScoreBar"></span></div>
<div class="score-row"><span>Cooling recovery <span class="meta">· 20%</span></span><strong id="recoveryScoreText">—</strong></div><div class="bar"><span id="recoveryScoreBar"></span></div>
<div class="score-row"><span>Energy efficiency <span class="meta">· 20%</span></span><strong id="energyScoreText">—</strong></div><div class="bar"><span id="energyScoreBar"></span></div>
</article>
<article class="glass section"><h3>7-day health history</h3><p class="section-intro">From on-device logged daily records</p>
<div class="daybars" id="dayBars" style="margin-bottom:22px"></div>
<div class="notice" id="maintenanceNotice">Collecting recovery history…</div>
</article>
</div>

<div class="section-label"><div><h2>Energy &amp; usage insights</h2><p>Model-based estimate · door usage pattern</p></div></div>
<section class="grid" style="grid-template-columns:repeat(3,minmax(0,1fr))">
<article class="glass card"><div class="card-top"><span class="card-title">Estimated energy</span><span class="icon">ϟ</span></div><div class="metric mono" id="energyValue">— <span class="unit">kWh</span></div><div class="bar" style="margin-top:10px"><span id="energyBar"></span></div><div class="meta" style="margin-top:8px">Efficiency: <strong id="energyEfficiency">—</strong>/100</div></article>
<article class="glass card"><div class="card-top"><span class="card-title">User behavior</span><span class="icon">☺</span></div><div class="metric" id="behaviorValue" style="font-size:22px">—</div><div class="meta">Peak hour: <strong id="peakHour">—</strong></div></article>
<article class="glass card"><div class="card-top"><span class="card-title">Predictive maintenance</span><span class="icon">⌁</span></div><div class="metric" id="maintenanceValue" style="font-size:20px">—</div><div class="meta" id="maintenanceMeta">Evaluating…</div><span class="pill warn" id="maintenancePill">HISTORY REQUIRED</span></article>
</section>

<footer class="footer"><span>SMART REFRIGERATOR · ESP32 EDGE COMPUTING</span><span>Local network only · No cloud required · <span id="footerState">Connecting…</span></span></footer>
</main>
<script>
const $=id=>document.getElementById(id);
function setText(id,v){$(id).textContent=v}
function bar(id,v){$(id).style.width=Math.max(0,Math.min(100,Number(v)||0))+'%'}
function pill(id,text,kind=''){setText(id,text);$(id).className='pill '+kind}
function score(v){return Number(v||0).toFixed(1)}

const RING_CIRC = 2*Math.PI*80;
function setRing(pct){
  const off = RING_CIRC - (Math.max(0,Math.min(100,pct))/100)*RING_CIRC;
  $('ringVal').style.strokeDashoffset = off;
}

function formatUptime(s){
  s=Number(s||0);const d=Math.floor(s/86400);s%=86400;const h=Math.floor(s/3600);s%=3600;const m=Math.floor(s/60);
  if(d>0)return d+'d '+h+'h';
  if(h>0)return h+'h '+m+'m';
  return m+'m';
}

async function refreshData(){
  try{
    const r=await fetch('/data',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);
    const d=await r.json();
    $('liveDot').style.background='var(--green)';
    setText('connectionText','LOCAL DEVICE · CONNECTED');
    setText('footerState','Live');
    setText('lastUpdated','Updated '+new Date().toLocaleTimeString());
    setText('dayStat','Day '+(d.day??'—'));
    setText('uptimeTag','Uptime '+formatUptime(d.uptimeSeconds));
    setText('minMaxTemp',(d.minTemp??'—')+' / '+(d.maxTemp??'—')+' °C');

    setText('healthBig',score(d.healthScore));
    setRing(d.healthScore);
    setText('healthStatus',d.healthScore>=85?'System health is excellent':d.healthScore>=70?'System health is good':d.healthScore>=50?'Monitor performance':'Performance needs attention');

    if(d.temperatureValid){
      $('temperatureValue').innerHTML=Number(d.temperature).toFixed(1)+' <span class="unit">°C</span>';
      setText('temperatureMeta','Sensor reading is available');
      pill('temperaturePill',d.temperature>=2&&d.temperature<=5?'IN TARGET':'CHECK TEMPERATURE',d.temperature>=2&&d.temperature<=5?'':'warn');
    }else{
      $('temperatureValue').innerHTML='-- <span class="unit">°C</span>';
      setText('temperatureMeta','No valid DS18B20 reading');
      pill('temperaturePill','SENSOR WARNING','bad');
    }

    if(d.humidityValid){
      $('humidityValue').innerHTML=Number(d.humidity).toFixed(1)+' <span class="unit">%</span>';
      setText('humidityMeta','DHT22 reading is available');
      pill('humidityPill','SENSOR ONLINE');
    }else{
      $('humidityValue').innerHTML='-- <span class="unit">%</span>';
      setText('humidityMeta','No valid DHT22 reading');
      pill('humidityPill','SENSOR WARNING','bad');
    }

    setText('doorValue',d.doorOpen?'OPEN':'CLOSED');
    setText('doorOpenings',d.doorOpenings??'—');
    pill('doorPill',d.doorStatus||'CLOSED',d.doorStatus==='ALERT'?'bad':d.doorStatus==='WARNING'?'warn':'');

    setText('recoveryCount',d.recoveryCount??'—');
    setText('recoveryValue',d.recoveryStatus||'MONITORING');
    pill('recoveryPill',d.recoveryStatus||'MONITORING');

    $('energyValue').innerHTML=Number(d.estimatedKWh||0).toFixed(3)+' <span class="unit">kWh</span>';
    setText('energyEfficiency',score(d.energyEfficiency));
    bar('energyBar',d.energyEfficiency);

    setText('maintenanceValue',d.predictiveMaintenance||'INSUFFICIENT HISTORY');
    setText('maintenanceMeta',d.predictiveMaintenanceMessage||'More valid history is needed');
    pill('maintenancePill',d.predictiveMaintenance==='MAINTENANCE ADVISED'?'ACTION ADVISED':'TREND ANALYSIS',d.predictiveMaintenance==='MAINTENANCE ADVISED'?'bad':'warn');
    setText('maintenanceNotice',d.predictiveMaintenanceMessage||'Collecting recovery history…');

    [['temperatureScore','tempScoreText','tempScoreBar'],['doorScore','doorScoreText','doorScoreBar'],
     ['recoveryScore','recoveryScoreText','recoveryScoreBar'],['energyScore','energyScoreText','energyScoreBar']]
     .forEach(a=>{setText(a[1],score(d[a[0]])+' / 100');bar(a[2],d[a[0]])});

    setText('behaviorValue',d.userBehavior||'—');
    setText('peakHour',d.peakUsageHour===undefined?'—':String(d.peakUsageHour).padStart(2,'0')+':00');

    const alertBar=$('alertBar');
    if(d.doorStatus==='ALERT'){
      alertBar.textContent='⚠ Door has been open too long — please close the refrigerator';
      alertBar.classList.add('show');
    }else if(d.predictiveMaintenance==='MAINTENANCE ADVISED'){
      alertBar.textContent='⚠ Predictive maintenance advised — cooling recovery time is trending upward';
      alertBar.classList.add('show');
    }else{
      alertBar.classList.remove('show');
    }
  }catch(e){
    $('liveDot').style.background='var(--red)';
    setText('connectionText','DEVICE CONNECTION LOST');
    setText('footerState','Waiting for ESP32');
    setText('lastUpdated','Unable to fetch live data');
  }
}

function drawSpark(values){
  const canvas=$('sparkCanvas');const wrap=canvas.parentElement;
  const w=wrap.clientWidth,h=wrap.clientHeight;
  const dpr=window.devicePixelRatio||1;
  canvas.width=w*dpr;canvas.height=h*dpr;
  canvas.style.width=w+'px';canvas.style.height=h+'px';
  const ctx=canvas.getContext('2d');ctx.scale(dpr,dpr);
  ctx.clearRect(0,0,w,h);
  if(!values||values.length<2){
    ctx.fillStyle='#8ea0bd';ctx.font='12px sans-serif';ctx.fillText('Collecting samples…',14,h/2);
    return;
  }
  const pad=14;
  const min=Math.min(...values),max=Math.max(...values);
  const range=(max-min)||1;
  const stepX=(w-pad*2)/(values.length-1);
  function yOf(v){return h-pad-((v-min)/range)*(h-pad*2)}

  const grad=ctx.createLinearGradient(0,0,0,h);
  grad.addColorStop(0,'rgba(79,224,234,0.35)');
  grad.addColorStop(1,'rgba(79,224,234,0.02)');
  ctx.beginPath();ctx.moveTo(pad,yOf(values[0]));
  values.forEach((v,i)=>ctx.lineTo(pad+i*stepX,yOf(v)));
  ctx.lineTo(pad+(values.length-1)*stepX,h-pad);ctx.lineTo(pad,h-pad);ctx.closePath();
  ctx.fillStyle=grad;ctx.fill();

  ctx.beginPath();ctx.moveTo(pad,yOf(values[0]));
  values.forEach((v,i)=>ctx.lineTo(pad+i*stepX,yOf(v)));
  ctx.strokeStyle='#4fe0ea';ctx.lineWidth=2;ctx.lineJoin='round';ctx.stroke();

  const lastX=pad+(values.length-1)*stepX,lastY=yOf(values[values.length-1]);
  ctx.beginPath();ctx.arc(lastX,lastY,4,0,Math.PI*2);ctx.fillStyle='#4fe0ea';ctx.fill();

  ctx.fillStyle='#8ea0bd';ctx.font='11px sans-serif';
  ctx.fillText(max.toFixed(1)+'°C',pad,16);
  ctx.fillText(min.toFixed(1)+'°C',pad,h-6);
}

async function refreshTrend(){
  try{const r=await fetch('/trend',{cache:'no-store'});const v=await r.json();drawSpark(v);}catch(e){}
}

function timeAgo(nowSec,atSec){
  const diff=Math.max(0,nowSec-atSec);
  if(diff<60)return diff+'s ago';
  if(diff<3600)return Math.floor(diff/60)+'m ago';
  return Math.floor(diff/3600)+'h ago';
}

async function refreshEvents(){
  try{
    const r=await fetch('/events',{cache:'no-store'});const events=await r.json();
    const nowSec=Math.floor(Date.now()/1000);
    if(!events.length){$('feed').innerHTML='<div class="feeditem"><span class="feeddot"></span><span class="feedtext">No events yet</span></div>';return}
    $('feed').innerHTML=events.map(e=>`<div class="feeditem"><span class="feeddot"></span><span class="feedtext">${e.m}</span><span class="feedtime">${timeAgo(nowSec,e.t)}</span></div>`).join('');
  }catch(e){}
}

async function refreshDayBars(){
  try{
    const r=await fetch('/dailyhistory',{cache:'no-store'});const days=await r.json();
    const recent=days.slice(-7);
    if(!recent.length){$('dayBars').innerHTML='<div class="meta">No completed days yet — health bars appear after day 1 finishes.</div>';return}
    const container=$('dayBars');container.style.position='relative';container.innerHTML='';
    recent.forEach(rec=>{
      const h=Math.max(4,(rec.healthScore||0)/100*120);
      const el=document.createElement('div');
      el.className='daybar';el.style.height=h+'px';
      el.innerHTML='<span class="lbl">D'+rec.day+'</span>';
      el.title='Day '+rec.day+': '+(rec.healthScore||0).toFixed(1)+'/100';
      container.appendChild(el);
    });
  }catch(e){}
}

refreshData();refreshTrend();refreshEvents();refreshDayBars();
setInterval(refreshData,3000);
setInterval(refreshTrend,4000);
setInterval(refreshEvents,6000);
setInterval(refreshDayBars,20000);
window.addEventListener('resize',refreshTrend);
</script></body></html>)HTML";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", html);
}

// ================================================================
// END OF PROGRAM
// ================================================================
