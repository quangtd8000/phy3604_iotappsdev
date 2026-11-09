/*
 * PHY3640 - IoT Gravimetric Coffee Brewer (ESP32-S3)
 * Wokwi Web / Arduino IDE standalone sketch
 * ESP32-S3 + HX711 + DS18B20 + Bơm PWM + Servo + Buzzer + MQTT
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>

// ===================== CẤU HÌNH HỆ THỐNG =====================

// Wi-Fi (Wokwi giả lập dùng "Wokwi-GUEST", không cần password)
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASS ""

// MQTT Broker (dùng HiveMQ công cộng)
#define MQTT_HOST "broker.hivemq.com"
#define MQTT_PORT 1883
#define TOPIC_TELEMETRY "phy3640/quang/coffee/telemetry"
#define TOPIC_COMMAND   "phy3640/quang/coffee/command"
#define TOPIC_STATUS    "phy3640/quang/coffee/status"
#define TELEMETRY_PERIOD_MS 250

// Chân GPIO trên ESP32-S3 DevKitC-1
#define PIN_HX711_DT   16
#define PIN_HX711_SCK  17
#define PIN_DS18B20     4
#define PIN_PUMP        6   // PWM điều tốc MOSFET bơm
#define PIN_SERVO       5   // PWM điều khiển Servo vòi
#define PIN_BUZZER      7   // Còi báo Buzzer
#define PIN_LED        15   // LED chỉ báo
#define PIN_BTN_TARE    1   // Nút trừ bì (Tare)
#define PIN_BTN_STOP    2   // Nút dừng khẩn cấp (Stop)

// Loadcell & Bộ lọc
#define HX711_CAL_FACTOR 420.0983f
#define EMA_ALPHA        0.30f
#define FLOW_WINDOW      6
#define FLOW_SAMPLE_MS   200

// Điều khiển bơm (P-Controller)
#define TARGET_FLOW_GPS  2.5f
#define FLOW_KP          12.0f
#define PUMP_DUTY_START  150
#define PUMP_DUTY_MIN    70
#define PUMP_DUTY_MAX    255
#define SLOWDOWN_ZONE_G  15.0f
#define STOP_OFFSET_G    1.0f
#define NO_FLOW_TIMEOUT_MS 15000
#define DRAWDOWN_TIMEOUT_MS 120000
#define DRAWDOWN_STABLE_MS  3000
#define DRAWDOWN_FLOW_GPS   0.15f

// Servo đảo vòi rót
#define SERVO_MIN_DEG 20
#define SERVO_MAX_DEG 70
#define SERVO_STEP_MS 60

// Ngưỡng nhiệt độ chuẩn
#define TEMP_MIN_C 88.0f
#define TEMP_MAX_C 94.0f

// ===================== MÁY TRẠNG THÁI =====================
enum State { ST_IDLE, ST_TARING, ST_BLOOM_POUR, ST_BLOOM_REST, ST_POURING, ST_DRAWDOWN, ST_DONE, ST_ERROR };
static const char* STATE_NAMES[] = {"IDLE", "TARING", "BLOOM_POUR", "BLOOM_REST", "POURING", "DRAWDOWN", "DONE", "ERROR"};

struct Recipe {
  String name = "Classic_PourOver";
  float doseG = 15.0f;
  float ratio = 15.0f;
  float bloomG = 30.0f;
  uint32_t bloomS = 35;
  float targetG() const { return doseG * ratio; }
} recipe;

// ===================== KHỞI TẠO ĐỐI TƯỢNG =====================
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
HX711 scale;
OneWire oneWire(PIN_DS18B20);
DallasTemperature ds18(&oneWire);
Servo nozzleServo;
ESP32PWM pumpPwm;

// ===================== BIẾN HỆ THỐNG =====================
State state = ST_IDLE;
String lastMessage = "Ready";

float weightG = 0, weightEma = 0, flowGps = 0;
float waterTempC = NAN;
int pumpDuty = 0;
int servoAngle = (SERVO_MIN_DEG + SERVO_MAX_DEG) / 2;
int servoDir = 1;

uint32_t brewStartMs = 0, stateStartMs = 0;
uint32_t lastProgressMs = 0;
float lastProgressG = 0;
uint32_t stableSinceMs = 0;

float histW[FLOW_WINDOW];
uint32_t histT[FLOW_WINDOW];
uint8_t histCount = 0, histHead = 0;

int beepToggles = 0;
uint32_t lastBeepMs = 0;

// ===================== HÀM ĐIỀU KHIỂN =====================
void beep(int n) { beepToggles = n * 2; }

void updateBeep() {
  if (beepToggles > 0 && millis() - lastBeepMs >= 120) {
    lastBeepMs = millis();
    digitalWrite(PIN_BUZZER, beepToggles % 2 == 0 ? HIGH : LOW);
    beepToggles--;
    if (beepToggles == 0) digitalWrite(PIN_BUZZER, LOW);
  }
}

void setPump(int duty) {
  pumpDuty = constrain(duty, 0, PUMP_DUTY_MAX);
  pumpPwm.write(pumpDuty);
  digitalWrite(PIN_LED, pumpDuty > 0 ? HIGH : LOW);
}

void setState(State s, const char* msg = nullptr) {
  state = s;
  stateStartMs = millis();
  lastProgressMs = millis();
  lastProgressG = weightG;
  if (msg) lastMessage = msg;
  Serial.printf("[STATE] %s - %s\n", STATE_NAMES[s], lastMessage.c_str());
}

void resetFlowHistory() {
  histCount = 0;
  histHead = 0;
  flowGps = 0;
}

void abortBrew(const char* msg) {
  setPump(0);
  setState(ST_IDLE, msg);
}

void readWeight() {
  if (!scale.is_ready()) return;
  weightG = scale.get_units(1);
  weightEma = EMA_ALPHA * weightG + (1.0f - EMA_ALPHA) * weightEma;
  weightG = weightEma;
}

void updateFlow() {
  static uint32_t lastSample = 0;
  if (millis() - lastSample < FLOW_SAMPLE_MS) return;
  lastSample = millis();

  histW[histHead] = weightG;
  histT[histHead] = lastSample;
  histHead = (histHead + 1) % FLOW_WINDOW;
  if (histCount < FLOW_WINDOW) histCount++;
  if (histCount < 2) { flowGps = 0; return; }

  uint8_t oldest = (histCount < FLOW_WINDOW) ? 0 : histHead;
  uint8_t newest = (histHead + FLOW_WINDOW - 1) % FLOW_WINDOW;
  float dt = (histT[newest] - histT[oldest]) / 1000.0f;
  if (dt > 0) flowGps = (histW[newest] - histW[oldest]) / dt;
}

void updateTemperature() {
  static uint32_t lastReq = 0;
  static bool waiting = false;
  if (!waiting && millis() - lastReq > 1000) {
    ds18.requestTemperatures();
    lastReq = millis();
    waiting = true;
  } else if (waiting && millis() - lastReq > 800) {
    float t = ds18.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C) waterTempC = t;
    waiting = false;
  }
}

void controlPump(float remainingG) {
  static uint32_t lastCtl = 0;
  if (millis() - lastCtl < FLOW_SAMPLE_MS) return;
  lastCtl = millis();

  float desired = TARGET_FLOW_GPS;
  if (remainingG < SLOWDOWN_ZONE_G) desired = max(0.8f, TARGET_FLOW_GPS * remainingG / SLOWDOWN_ZONE_G);
  float err = desired - flowGps;
  setPump((int)constrain(pumpDuty + FLOW_KP * err, PUMP_DUTY_MIN, PUMP_DUTY_MAX));
}

bool checkNoFlow() {
  if (weightG - lastProgressG > 0.5f) {
    lastProgressG = weightG;
    lastProgressMs = millis();
  }
  return millis() - lastProgressMs > NO_FLOW_TIMEOUT_MS;
}

void sweepServo(bool active) {
  static uint32_t last = 0;
  if (!active || millis() - last < SERVO_STEP_MS) return;
  last = millis();
  servoAngle += servoDir * 2;
  if (servoAngle >= SERVO_MAX_DEG) { servoAngle = SERVO_MAX_DEG; servoDir = -1; }
  if (servoAngle <= SERVO_MIN_DEG) { servoAngle = SERVO_MIN_DEG; servoDir = 1; }
  nozzleServo.write(servoAngle);
}

void startBrew() {
  if (state != ST_IDLE && state != ST_DONE && state != ST_ERROR) {
    lastMessage = "Dang pha, bo qua lenh START";
    return;
  }
  setPump(0);
  scale.tare(10);
  weightEma = 0;
  weightG = 0;
  resetFlowHistory();
  brewStartMs = millis();
  setState(ST_TARING, "Auto-tare");
}

void runStateMachine() {
  const float target = recipe.targetG();
  switch (state) {
    case ST_IDLE:
    case ST_DONE:
    case ST_ERROR:
      setPump(0);
      break;

    case ST_TARING:
      if (millis() - stateStartMs > 800) {
        resetFlowHistory();
        setPump(PUMP_DUTY_START);
        setState(ST_BLOOM_POUR, "Stage 1: Bloom - bom nuoc u");
      }
      break;

    case ST_BLOOM_POUR:
      controlPump(recipe.bloomG - weightG);
      if (checkNoFlow()) { setPump(0); setState(ST_ERROR, "Loi: khong co dong chay"); beep(5); break; }
      if (weightG >= recipe.bloomG - STOP_OFFSET_G) {
        setPump(0);
        setState(ST_BLOOM_REST, "Stage 1: Bloom - nghi thoat CO2");
      }
      break;

    case ST_BLOOM_REST:
      if (millis() - stateStartMs >= recipe.bloomS * 1000UL) {
        beep(1);
        setPump(PUMP_DUTY_START);
        setState(ST_POURING, "Stage 2: Active extraction");
      }
      break;

    case ST_POURING:
      controlPump(target - weightG);
      if (checkNoFlow()) { setPump(0); setState(ST_ERROR, "Loi: khong co dong chay"); beep(5); break; }
      if (weightG > target + 20.0f) { setPump(0); setState(ST_ERROR, "Loi: qua khoi luong muc tieu"); beep(5); break; }
      if (weightG >= target - STOP_OFFSET_G) {
        setPump(0);
        stableSinceMs = 0;
        setState(ST_DRAWDOWN, "Stage 3: Drawdown");
      }
      break;

    case ST_DRAWDOWN:
      if (fabsf(flowGps) < DRAWDOWN_FLOW_GPS) {
        if (stableSinceMs == 0) stableSinceMs = millis();
      } else {
        stableSinceMs = 0;
      }
      if ((stableSinceMs && millis() - stableSinceMs > DRAWDOWN_STABLE_MS) ||
          millis() - stateStartMs > DRAWDOWN_TIMEOUT_MS) {
        beep(3);
        setState(ST_DONE, "Hoan tat! Ly ca phe da san sang");
      }
      break;
  }
  sweepServo(state == ST_BLOOM_POUR || state == ST_POURING);
}

void onMqtt(char* topic, byte* payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) { Serial.println("[MQTT] JSON loi"); return; }
  String action = doc["action"] | "";
  Serial.printf("[MQTT] action=%s\n", action.c_str());

  if (action == "START_BREW") {
    recipe.name   = doc["recipe_name"] | "Custom";
    recipe.doseG  = doc["coffee_dose_g"] | 15.0f;
    recipe.ratio  = doc["ratio"] | 15.0f;
    recipe.bloomG = doc["bloom_water_g"] | (recipe.doseG * 2.0f);
    recipe.bloomS = doc["bloom_time_s"] | 35;
    if (recipe.doseG <= 0 || recipe.ratio <= 0 || recipe.bloomG >= recipe.targetG()) {
      lastMessage = "Cong thuc khong hop le";
      return;
    }
    startBrew();
  } else if (action == "STOP") {
    abortBrew("Da dung boi nguoi dung");
  } else if (action == "TARE") {
    if (state == ST_IDLE || state == ST_DONE || state == ST_ERROR) {
      scale.tare(10);
      weightEma = 0;
      weightG = 0;
      lastMessage = "Tare";
    }
  }
}

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  static uint32_t lastTry = 0;
  if (millis() - lastTry < 5000) return;
  lastTry = millis();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void ensureMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  static uint32_t lastTry = 0;
  if (millis() - lastTry < 3000) return;
  lastTry = millis();
  String cid = "phy3640-coffee-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqtt.connect(cid.c_str(), nullptr, nullptr, TOPIC_STATUS, 0, true, "offline")) {
    mqtt.subscribe(TOPIC_COMMAND);
    mqtt.publish(TOPIC_STATUS, "online", true);
    Serial.println("[MQTT] connected");
  }
}

void publishTelemetry() {
  static uint32_t last = 0;
  if (!mqtt.connected() || millis() - last < TELEMETRY_PERIOD_MS) return;
  last = millis();

  JsonDocument doc;
  doc["state"] = STATE_NAMES[state];
  doc["recipe"] = recipe.name;
  doc["weight_g"] = roundf(weightG * 10) / 10;
  doc["target_g"] = recipe.targetG();
  doc["flow_rate_gps"] = roundf(max(flowGps, 0.0f) * 10) / 10;
  if (!isnan(waterTempC)) {
    doc["water_temp_c"] = roundf(waterTempC * 10) / 10;
    doc["temp_ok"] = waterTempC >= TEMP_MIN_C && waterTempC <= TEMP_MAX_C;
  }
  bool running = state != ST_IDLE && state != ST_DONE && state != ST_ERROR;
  doc["elapsed_s"] = running ? (millis() - brewStartMs) / 1000 : 0;
  doc["servo_angle"] = servoAngle;
  doc["pump_duty"] = pumpDuty;
  doc["message"] = lastMessage;

  char buf[320];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(TOPIC_TELEMETRY, (const uint8_t*)buf, n, false);
}

void handleButtons() {
  static uint32_t lastTare = 0, lastStop = 0;
  if (digitalRead(PIN_BTN_STOP) == LOW && millis() - lastStop > 300) {
    lastStop = millis();
    abortBrew("Dung khan cap (nut nhan)");
  }
  if (digitalRead(PIN_BTN_TARE) == LOW && millis() - lastTare > 300) {
    lastTare = millis();
    if (state == ST_IDLE || state == ST_DONE || state == ST_ERROR) {
      scale.tare(10);
      weightEma = 0;
      weightG = 0;
      lastMessage = "Tare";
    }
  }
}

// ===================== SETUP & LOOP =====================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BTN_TARE, INPUT_PULLUP);
  pinMode(PIN_BTN_STOP, INPUT_PULLUP);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  nozzleServo.setPeriodHertz(50);
  nozzleServo.attach(PIN_SERVO, 500, 2400);
  nozzleServo.write(servoAngle);
  pumpPwm.attachPin(PIN_PUMP, 1000, 8);
  setPump(0);

  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  scale.set_scale(HX711_CAL_FACTOR);
  scale.tare(10);

  ds18.begin();
  ds18.setWaitForConversion(false);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);
  mqtt.setBufferSize(512);

  beep(2);
  Serial.println("PHY3640 IoT Gravimetric Coffee Brewer (ESP32-S3) - ready");
}

void loop() {
  ensureWifi();
  ensureMqtt();
  mqtt.loop();

  readWeight();
  updateFlow();
  updateTemperature();
  handleButtons();
  runStateMachine();
  updateBeep();
  publishTelemetry();
}
