#include <Arduino.h>
#include <PZEM004Tv30.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h> 
#include <Adafruit_SH110X.h>

// --- KONFIGURASI WIFI & MQTT ---
const char *ssid = "Mess 6";
const char *password = "w4k4w4k4";
const char *mqtt1_server = "192.168.110.199";
const int mqtt1_port = 1883;
const char *mqtt1_user = "";
const char *mqtt1_password = "";

const char *mqtt2_server = "134.185.90.201";
const int mqtt2_port = 1883;
const char *mqtt2_user = "wimboro";
const char *mqtt2_password = "w4k4w4k4";

// --- KONFIGURASI OTA ---
const char *OTA_HOSTNAME = "pzem-esp32c3";
const char *OTA_PASSWORD = "";

// Topic MQTT untuk Home Assistant.
const char *DEVICE_ID = "pzem_004t_esp32";
const char *MQTT_STATE_TOPIC = "pzem_004t_esp32/state";
const char *MQTT_STATUS_TOPIC = "pzem_004t_esp32/status";
const char *MQTT_RESET_ENERGY_TOPIC = "pzem_004t_esp32/reset_energy/set";

// ESP32-C3 Super Mini:
// GPIO4 menerima data dari TX PZEM, GPIO5 mengirim data ke RX PZEM.
// Ubah pin ini jika wiring Anda berbeda.
static const uint8_t PZEM_RX_PIN = 5;
static const uint8_t PZEM_TX_PIN = 4;

// OLED SH1106 I2C 1.3 inch 128x64.
// Wiring default: GPIO6 -> SDA, GPIO7 -> SCK/SCL, 3V3 -> VCC, GND -> GND.
static const bool USE_OLED = true;
static const uint8_t OLED_SDA_PIN = 6;
static const uint8_t OLED_SCL_PIN = 7;
static const uint8_t OLED_ADDRESS = 0x3C;
static const int OLED_WIDTH = 128;
static const int OLED_HEIGHT = 64;

static const unsigned long READ_INTERVAL_MS = 5000;
static const uint16_t MQTT_KEEP_ALIVE_SECONDS = 60;
static const uint16_t MQTT_SOCKET_TIMEOUT_SECONDS = 5;
HardwareSerial PzemSerial(1);
PZEM004Tv30 pzem(PzemSerial, PZEM_RX_PIN, PZEM_TX_PIN);
WiFiClient wifiClient;
WiFiClient wifiClient2;
PubSubClient mqttClient(wifiClient);
PubSubClient mqttClient2(wifiClient2);
Adafruit_SH1106G display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

struct EnergyReading {
  float voltage;
  float current;
  float power;
  float energy;
  float frequency;
  float powerFactor;
  float chipTemperature;
  bool valid;
};

EnergyReading lastReading = {};
unsigned long lastReadAt = 0;
unsigned long lastMqttConnectAttempt = 0;
unsigned long lastMqtt2ConnectAttempt = 0;
bool mqtt1DiscoveryPublished = false;
bool mqtt2DiscoveryPublished = false;
bool oledReady = false;
bool resetEnergyRequested = false;
bool otaInProgress = false;

String valueOrDash(float value, unsigned int decimals) {
  if (isnan(value)) {
    return "-";
  }
  return String(value, decimals);
}

String valueWithUnit(float value, unsigned int decimals, const char *unit) {
  String text = valueOrDash(value, decimals);
  text += unit;
  return text;
}

void drawTextLine(int16_t x, int16_t y, const __FlashStringHelper *label, const String &value) {
  display.setCursor(x, y);
  display.print(label);
  display.print(value);
}

void drawOledHeader(const __FlashStringHelper *title) {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(title);
  display.setCursor(54, 0);
  display.print(valueWithUnit(lastReading.chipTemperature, 1, " C"));
  display.setCursor(100, 0);
  display.print(WiFi.status() == WL_CONNECTED ? F("W") : F("-"));
  display.setCursor(116, 0);
  if (mqttClient.connected() && mqttClient2.connected()) {
    display.print(F("B"));
  } else if (mqttClient.connected()) {
    display.print(F("1"));
  } else if (mqttClient2.connected()) {
    display.print(F("2"));
  } else {
    display.print(F("-"));
  }
  display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);
}

void drawOledStatus(const __FlashStringHelper *line1, const __FlashStringHelper *line2) {
  if (!oledReady) {
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println(F("PZEM-004T ESP32-C3"));
  display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);
  display.setCursor(0, 22);
  display.println(line1);
  display.setCursor(0, 36);
  display.println(line2);
  display.display();
}

void drawOledStatusText(const __FlashStringHelper *line1, const char *line2) {
  if (!oledReady) {
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println(F("PZEM-004T ESP32-C3"));
  display.drawLine(0, 10, OLED_WIDTH - 1, 10, SH110X_WHITE);
  display.setCursor(0, 22);
  display.println(line1);
  display.setCursor(0, 36);
  display.println(line2);
  display.display();
}

void updateOled() {
  if (!oledReady) {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  drawOledHeader(F("BV2 A11"));

  if (!lastReading.valid) {
    display.setTextSize(1);
    display.setCursor(0, 22);
    display.println(F("Data PZEM belum valid"));
    display.setCursor(0, 36);
    display.println(F("Cek TX/RX dan supply"));
    display.display();
    return;
  }

  display.setTextSize(1);
  display.setCursor(0, 15);
  display.print(F("V"));
  display.setCursor(0, 33);
  display.print(F("W"));
  display.setCursor(0, 51);
  display.print(F("E"));

  display.setTextSize(2);
  display.setCursor(18, 12);
  display.print(valueOrDash(lastReading.voltage, 1));
  display.setTextSize(1);
  display.setCursor(104, 18);
  display.print(F("V"));

  display.setTextSize(2);
  display.setCursor(18, 30);
  display.print(valueOrDash(lastReading.power, 1));
  display.setTextSize(1);
  display.setCursor(104, 36);
  display.print(F("W"));

  display.setTextSize(1);
  display.setCursor(18, 52);
  display.print(valueOrDash(lastReading.energy, 3));
  display.setCursor(86, 52);
  display.print(F("kWh"));

  display.display();
}

void setupOled() {
  if (!USE_OLED) {
    return;
  }

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  Wire.setClock(100000);
  oledReady = display.begin(OLED_ADDRESS, true);

  if (!oledReady) {
    Serial.println(F("OLED SH1106 tidak terdeteksi. Cek alamat 0x3C/0x3D dan wiring I2C."));
    return;
  }

  display.setContrast(255);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println(F("HALO WIMBORO"));
  display.println(F("OLED OK"));
  display.println(F("Starting..."));
  display.display();
  Serial.println(F("OLED SH1106 aktif."));
}

EnergyReading readPzem() {
  EnergyReading reading;
  reading.voltage = pzem.voltage();
  reading.current = pzem.current();
  reading.power = pzem.power();
  reading.energy = pzem.energy();
  reading.frequency = pzem.frequency();
  reading.powerFactor = pzem.pf();
  reading.chipTemperature = temperatureRead();

  reading.valid = !isnan(reading.voltage) &&
                  !isnan(reading.current) &&
                  !isnan(reading.power) &&
                  !isnan(reading.energy) &&
                  !isnan(reading.frequency) &&
                  !isnan(reading.powerFactor);
  return reading;
}

void printReading(const EnergyReading &reading) {
  Serial.println(F("================================"));
  Serial.print(F("Suhu Chip    : "));
  Serial.print(reading.chipTemperature, 1);
  Serial.println(F(" C"));

  if (!reading.valid) {
    Serial.println(F("Gagal membaca PZEM-004T. Cek wiring, baudrate, dan supply modul."));
    return;
  }

  Serial.print(F("Tegangan     : "));
  Serial.print(reading.voltage, 1);
  Serial.println(F(" V"));

  Serial.print(F("Arus         : "));
  Serial.print(reading.current, 3);
  Serial.println(F(" A"));

  Serial.print(F("Daya         : "));
  Serial.print(reading.power, 1);
  Serial.println(F(" W"));

  Serial.print(F("Energi       : "));
  Serial.print(reading.energy, 3);
  Serial.println(F(" kWh"));

  Serial.print(F("Frekuensi    : "));
  Serial.print(reading.frequency, 1);
  Serial.println(F(" Hz"));

  Serial.print(F("Power Factor : "));
  Serial.println(reading.powerFactor, 2);

}

String buildJson() {
  String json;
  json.reserve(220);
  json += F("{\"valid\":");
  json += lastReading.valid ? F("true") : F("false");
  json += F(",\"voltage\":");
  json += isnan(lastReading.voltage) ? F("null") : String(lastReading.voltage, 1);
  json += F(",\"current\":");
  json += isnan(lastReading.current) ? F("null") : String(lastReading.current, 3);
  json += F(",\"power\":");
  json += isnan(lastReading.power) ? F("null") : String(lastReading.power, 1);
  json += F(",\"energy\":");
  json += isnan(lastReading.energy) ? F("null") : String(lastReading.energy, 3);
  json += F(",\"frequency\":");
  json += isnan(lastReading.frequency) ? F("null") : String(lastReading.frequency, 1);
  json += F(",\"power_factor\":");
  json += isnan(lastReading.powerFactor) ? F("null") : String(lastReading.powerFactor, 2);
  json += F(",\"chip_temperature\":");
  json += isnan(lastReading.chipTemperature) ? F("null") : String(lastReading.chipTemperature, 1);
  json += F("}");
  return json;
}

void publishDiscoverySensor(PubSubClient &client,
                            const char *objectId,
                            const char *name,
                            const char *stateKey,
                            const char *unit,
                            const char *deviceClass,
                            const char *stateClass) {
  String topic = String("homeassistant/sensor/") + DEVICE_ID + "/" + objectId + "/config";
  String payload;
  payload.reserve(760);

  payload += F("{\"name\":\"");
  payload += name;
  payload += F("\",\"uniq_id\":\"");
  payload += DEVICE_ID;
  payload += F("_");
  payload += objectId;
  payload += F("\",\"stat_t\":\"");
  payload += MQTT_STATE_TOPIC;
  payload += F("\",\"avty_t\":\"");
  payload += MQTT_STATUS_TOPIC;
  payload += F("\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"");

  if (strlen(unit) > 0) {
    payload += F(",\"unit_of_meas\":\"");
    payload += unit;
    payload += F("\"");
  }

  if (strlen(deviceClass) > 0) {
    payload += F(",\"dev_cla\":\"");
    payload += deviceClass;
    payload += F("\"");
  }

  if (strlen(stateClass) > 0) {
    payload += F(",\"stat_cla\":\"");
    payload += stateClass;
    payload += F("\"");
  }

  payload += F(",\"val_tpl\":\"{{ value_json.");
  payload += stateKey;
  payload += F(" }}\"");
  payload += F(",\"dev\":{\"ids\":[\"");
  payload += DEVICE_ID;
  payload += F("\"],\"name\":\"PZEM-004T ESP32-C3\",\"mf\":\"DIY\",\"mdl\":\"PZEM-004T 100A + ESP32-C3\"}}");

  client.publish(topic.c_str(), payload.c_str(), true);
}

void publishResetEnergyButtonDiscovery(PubSubClient &client) {
  String topic = String("homeassistant/button/") + DEVICE_ID + "/reset_energy/config";
  String payload;
  payload.reserve(620);

  payload += F("{\"name\":\"Reset PZEM Energy\"");
  payload += F(",\"uniq_id\":\"");
  payload += DEVICE_ID;
  payload += F("_reset_energy\"");
  payload += F(",\"cmd_t\":\"");
  payload += MQTT_RESET_ENERGY_TOPIC;
  payload += F("\",\"pl_prs\":\"RESET\"");
  payload += F(",\"avty_t\":\"");
  payload += MQTT_STATUS_TOPIC;
  payload += F("\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"");
  payload += F(",\"dev\":{\"ids\":[\"");
  payload += DEVICE_ID;
  payload += F("\"],\"name\":\"PZEM-004T ESP32-C3\",\"mf\":\"DIY\",\"mdl\":\"PZEM-004T 100A + ESP32-C3\"}}");

  client.publish(topic.c_str(), payload.c_str(), true);
}

void publishHomeAssistantDiscovery(PubSubClient &client, const char *brokerName) {
  publishDiscoverySensor(client, "voltage", "PZEM Voltage", "voltage", "V", "voltage", "measurement");
  publishDiscoverySensor(client, "current", "PZEM Current", "current", "A", "current", "measurement");
  publishDiscoverySensor(client, "power", "PZEM Power", "power", "W", "power", "measurement");
  publishDiscoverySensor(client, "energy", "PZEM Energy", "energy", "kWh", "energy", "total_increasing");
  publishDiscoverySensor(client, "frequency", "PZEM Frequency", "frequency", "Hz", "frequency", "measurement");
  publishDiscoverySensor(client, "power_factor", "PZEM Power Factor", "power_factor", "", "", "measurement");
  publishDiscoverySensor(client, "chip_temperature", "ESP32 Chip Temperature", "chip_temperature", "\xC2\xB0""C", "temperature", "measurement");
  publishResetEnergyButtonDiscovery(client);
  Serial.print(F("MQTT Discovery Home Assistant terkirim ke "));
  Serial.println(brokerName);
}

void publishMqttState() {
  String payload = buildJson();
  bool published = false;

  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_STATE_TOPIC, payload.c_str(), true);
    published = true;
  }

  if (mqttClient2.connected()) {
    mqttClient2.publish(MQTT_STATE_TOPIC, payload.c_str(), true);
    published = true;
  }

  if (published) {
    Serial.println(F("Data terkirim ke MQTT."));
  }
}

void resetEnergyCounter() {
  pzem.resetEnergy();
  delay(200);
  lastReading = readPzem();
  publishMqttState();
  updateOled();
  Serial.println(F("Energi PZEM direset dari command MQTT."));
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    message += static_cast<char>(payload[i]);
  }

  if (String(topic) == MQTT_RESET_ENERGY_TOPIC && message == F("RESET")) {
    resetEnergyRequested = true;
  }
}

void reconnectMqttClientIfNeeded(PubSubClient &client,
                                 const char *server,
                                 int port,
                                 const char *user,
                                 const char *passwordValue,
                                 unsigned long &lastAttempt,
                                 const char *brokerName,
                                 const char *clientSuffix,
                                 bool &discoveryPublished) {
  if (WiFi.status() != WL_CONNECTED || client.connected()) {
    return;
  }

  unsigned long now = millis();
  if (now - lastAttempt < 5000) {
    return;
  }
  lastAttempt = now;

  Serial.print(F("Menghubungkan MQTT ke "));
  Serial.print(server);
  Serial.print(F(":"));
  Serial.print(port);
  Serial.print(F(" ("));
  Serial.print(brokerName);
  Serial.print(F(")"));
  Serial.print(F(" ... "));

  String clientId = String(DEVICE_ID) + "-" + clientSuffix + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool connected;
  if (strlen(user) > 0) {
    connected = client.connect(clientId.c_str(),
                               user,
                               passwordValue,
                               MQTT_STATUS_TOPIC,
                               1,
                               true,
                               "offline");
  } else {
    connected = client.connect(clientId.c_str(),
                               MQTT_STATUS_TOPIC,
                               1,
                               true,
                               "offline");
  }

  if (connected) {
    Serial.println(F("terhubung."));
    client.publish(MQTT_STATUS_TOPIC, "online", true);
    client.subscribe(MQTT_RESET_ENERGY_TOPIC);
    if (!discoveryPublished) {
      publishHomeAssistantDiscovery(client, brokerName);
      discoveryPublished = true;
    } else {
      Serial.print(F("MQTT Discovery dilewati untuk "));
      Serial.print(brokerName);
      Serial.println(F(" (sudah terkirim)."));
    }
    publishMqttState();
    updateOled();
  } else {
    Serial.print(F("gagal, rc="));
    Serial.println(client.state());
  }
}

void reconnectMqttIfNeeded() {
  reconnectMqttClientIfNeeded(mqttClient,
                              mqtt1_server,
                              mqtt1_port,
                              mqtt1_user,
                              mqtt1_password,
                              lastMqttConnectAttempt,
                              "broker 1",
                              "mqtt1",
                              mqtt1DiscoveryPublished);

  reconnectMqttClientIfNeeded(mqttClient2,
                              mqtt2_server,
                              mqtt2_port,
                              mqtt2_user,
                              mqtt2_password,
                              lastMqtt2ConnectAttempt,
                              "broker 2",
                              "mqtt2",
                              mqtt2DiscoveryPublished);
}

void enterOtaMode() {
  if (otaInProgress) {
    return;
  }

  otaInProgress = true;
  Serial.println(F("OTA mulai. PZEM dan MQTT dihentikan sementara."));

  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_STATUS_TOPIC, "offline", true);
    mqttClient.disconnect();
  }

  if (mqttClient2.connected()) {
    mqttClient2.publish(MQTT_STATUS_TOPIC, "offline", true);
    mqttClient2.disconnect();
  }

  PzemSerial.end();
  drawOledStatus(F("OTA update"), F("PZEM/MQTT pause"));
}

void recoverFromOtaError() {
  otaInProgress = false;
  PzemSerial.begin(9600, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);
  lastReadAt = millis();
}

void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);

  if (strlen(OTA_PASSWORD) > 0) {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    enterOtaMode();
  });

  ArduinoOTA.onEnd([]() {
    Serial.println(F("OTA selesai. Restart..."));
    drawOledStatus(F("OTA update"), F("Selesai"));
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    unsigned int percent = (progress * 100) / total;
    static unsigned int lastPercent = 101;

    if (percent != lastPercent && percent % 10 == 0) {
      lastPercent = percent;
      Serial.print(F("OTA progress: "));
      Serial.print(percent);
      Serial.println(F("%"));

      String text = String(percent) + "%";
      drawOledStatusText(F("OTA update"), text.c_str());
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.print(F("OTA error: "));
    Serial.println(static_cast<int>(error));
    recoverFromOtaError();
    drawOledStatus(F("OTA error"), F("Cek koneksi"));
  });

  ArduinoOTA.begin();
  Serial.print(F("OTA aktif. Hostname: "));
  Serial.println(OTA_HOSTNAME);
}

void connectWifiIfConfigured() {
  if (strlen(ssid) == 0) {
    Serial.println(F("WiFi belum diisi. Monitoring hanya lewat Serial Monitor."));
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print(F("Menghubungkan WiFi"));
  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 20000) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    setupOTA();
    mqttClient.setServer(mqtt1_server, mqtt1_port);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(1024);
    mqttClient.setKeepAlive(MQTT_KEEP_ALIVE_SECONDS);
    mqttClient.setSocketTimeout(MQTT_SOCKET_TIMEOUT_SECONDS);
    mqttClient2.setServer(mqtt2_server, mqtt2_port);
    mqttClient2.setCallback(mqttCallback);
    mqttClient2.setBufferSize(1024);
    mqttClient2.setKeepAlive(MQTT_KEEP_ALIVE_SECONDS);
    mqttClient2.setSocketTimeout(MQTT_SOCKET_TIMEOUT_SECONDS);
    drawOledStatus(F("WiFi terhubung"), F("MQTT connecting..."));
  } else {
    Serial.println(F("WiFi gagal terhubung. Monitoring tetap berjalan lewat Serial Monitor."));
    drawOledStatus(F("WiFi gagal"), F("Serial tetap aktif"));
  }
}

void setup() {
  Serial.begin(115200);

  // ESP32-C3 memakai native USB pada banyak board Super Mini.
  // Tunggu sebentar supaya Serial Monitor sempat terhubung setelah reset/upload.
  unsigned long serialStartedAt = millis();
  while (!Serial && millis() - serialStartedAt < 5000) {
    delay(10);
  }

  Serial.println();
  Serial.println(F("Monitoring Energi Listrik - PZEM-004T + ESP32-C3"));
  Serial.println(F("Serial Monitor: 115200 baud"));
  Serial.flush();

  lastReading.chipTemperature = temperatureRead();

  setupOled();
  drawOledStatus(F("Menghubungkan WiFi"), F("Mohon tunggu..."));

  connectWifiIfConfigured();
  updateOled();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }

  if (otaInProgress) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    reconnectMqttIfNeeded();
    mqttClient.loop();
    mqttClient2.loop();
  }

  if (resetEnergyRequested) {
    resetEnergyRequested = false;
    resetEnergyCounter();
  }

  unsigned long now = millis();
  if (now - lastReadAt >= READ_INTERVAL_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      for (uint8_t i = 0; i < 20; i++) {
        ArduinoOTA.handle();
        delay(1);
        if (otaInProgress) {
          return;
        }
      }
    }

    lastReadAt = now;
    lastReading = readPzem();
    printReading(lastReading);
    publishMqttState();
    updateOled();
  }
}
