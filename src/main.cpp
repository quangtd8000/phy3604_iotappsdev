/*
 * PHY3640 - IoT Gravimetric Coffee Brewer (ESP32-S3)
 * ESP32-S3 + HX711 loadcell + DS18B20 + bơm PWM + servo + buzzer, giao tiếp MQTT.
 *
 * Điều khiển vòng kín theo khối lượng:
 *   IDLE -> TARING -> BLOOM_POUR -> BLOOM_REST -> POURING -> DRAWDOWN -> DONE
 *   (ERROR khi không có dòng chảy / quá khối lượng; ABORT bằng lệnh STOP hoặc nút dừng)
 */
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include "config.h"

// ===================== Trạng thái =====================
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

// ===================== Đối tượng phần cứng =====================
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
HX711 scale;
OneWire oneWire(PIN_DS18B20);
DallasTemperature ds18(&oneWire);
Servo nozzleServo;
ESP32PWM pumpPwm;
ESP32PWM buzzerPwm;

// ===================== Biến toàn cục =====================
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

// Bộ đệm vòng để tính lưu lượng = (m(t) - m(t-Δt)) / Δt
float histW[FLOW_WINDOW];
uint32_t histT[FLOW_WINDOW];
uint8_t histCount = 0, histHead = 0;

// Buzzer không chặn
int beepToggles = 0;
uint32_t lastBeepMs = 0;

#if SIMULATION_MODE
// Mô hình vật lý đơn giản của bơm: lưu lượng bám theo duty với quán tính bậc nhất
float simMassG = 0, simFlowGps = 0;
uint32_t simLastMs = 0;
#endif

// ===================== Tiện ích =====================
void beep(int n) { beepToggles = n * 2; }

void buzzer(bool on) {
  // Phát tone 2 kHz (50% duty) để kêu được cả với buzzer thụ động và buzzer Wokwi
  buzzerPwm.write(on ? 512 : 0);
}

void updateBeep() {
  if (beepToggles > 0 && millis() - lastBeepMs >= 120) {
    lastBeepMs = millis();
    buzzer(beepToggles % 2 == 0);
    beepToggles--;
    if (beepToggles == 0) buzzer(false);
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

// ===================== Cảm biến =====================
void tareScale() {
#if SIMULATION_MODE
  simMassG = 0;
#else
  scale.tare(10);
#endif
  weightEma = 0;
  weightG = 0;
}

#if SIMULATION_MODE
void updateSimPlant() {
  uint32_t now = millis();
  float dt = (now - simLastMs) / 1000.0f;
  simLastMs = now;
  if (dt <= 0 || dt > 0.5f) return;
  float target = pumpDuty > SIM_DEAD_DUTY
                     ? (pumpDuty - SIM_DEAD_DUTY) * SIM_MAX_FLOW_GPS / (255 - SIM_DEAD_DUTY)
                     : 0.0f;
  simFlowGps += (target - simFlowGps) * dt / SIM_PUMP_TAU_S;
  simMassG += simFlowGps * dt;
}
#endif

void readWeight() {
#if SIMULATION_MODE
  static uint32_t lastRead = 0;
  updateSimPlant();
  if (millis() - lastRead < 100) return;  // giống tốc độ 10 SPS của HX711
  lastRead = millis();
  float raw = simMassG + SIM_NOISE_G * (random(-100, 101) / 100.0f);
#else
  if (!scale.is_ready()) return;
  float raw = scale.get_units(1);
#endif
  weightEma = EMA_ALPHA * raw + (1.0f - EMA_ALPHA) * weightEma;
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

// ===================== Điều khiển bơm (dạng gia số / velocity-form, tương đương khâu I) =====================
void controlPump(float remainingG) {
  static uint32_t lastCtl = 0;
  if (millis() - lastCtl < FLOW_SAMPLE_MS) return;
  lastCtl = millis();

  float desired = TARGET_FLOW_GPS;
  if (remainingG < SLOWDOWN_ZONE_G) desired = max(0.8f, TARGET_FLOW_GPS * remainingG / SLOWDOWN_ZONE_G);
  float err = desired - flowGps;
  setPump((int)constrain(pumpDuty + FLOW_KI * err, PUMP_DUTY_MIN, PUMP_DUTY_MAX));
}

// Kiểm tra bơm chạy mà khối lượng không tăng (hết nước / lệch cốc)
bool checkNoFlow() {
  if (weightG - lastProgressG > 0.5f) {
    lastProgressG = weightG;
    lastProgressMs = millis();
  }
  return millis() - lastProgressMs > NO_FLOW_TIMEOUT_MS;
}

// ===================== Servo quét vòi =====================
void sweepServo(bool active) {
  static uint32_t last = 0;
  if (!active || millis() - last < SERVO_STEP_MS) return;
  last = millis();
  servoAngle += servoDir * 2;
  if (servoAngle >= SERVO_MAX_DEG) { servoAngle = SERVO_MAX_DEG; servoDir = -1; }
  if (servoAngle <= SERVO_MIN_DEG) { servoAngle = SERVO_MIN_DEG; servoDir = 1; }
  nozzleServo.write(servoAngle);
}

// ===================== State machine =====================
void startBrew() {
  if (state != ST_IDLE && state != ST_DONE && state != ST_ERROR) {
    lastMessage = "Dang pha, bo qua lenh START";
    return;
  }
  setPump(0);
  tareScale();
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

// ===================== MQTT =====================
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
      tareScale();
      lastMessage = "Tare";
    }
  }
}

void beginWifi() {
  if (strcmp(WIFI_SSID, "Wokwi-GUEST") == 0) {
    // Kênh 6 là kênh AP ảo của Wokwi -> bỏ qua quét 13 kênh, kết nối tức thì
    WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

void ensureWifi() {
  static bool wasConnected = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (!wasConnected) {
      wasConnected = true;
      Serial.printf("\n[WiFi] Da ket noi! IP: %s\n", WiFi.localIP().toString().c_str());
    }
    return;
  }
  wasConnected = false;
  static uint32_t lastTry = 0;
  if (millis() - lastTry < 15000) return;
  lastTry = millis();
  Serial.printf("[WiFi] Dang ket noi lai %s...\n", WIFI_SSID);
  beginWifi();
}

void ensureMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  static uint32_t lastTry = 0;
  if (millis() - lastTry < 3000) return;
  lastTry = millis();
  String cid = "phy3640-coffee-" + String((uint32_t)ESP.getEfuseMac(), HEX) + "-" + String(random(1000, 9999));
  Serial.printf("[MQTT] Dang ket noi %s:%d ...\n", MQTT_HOST, MQTT_PORT);
  if (mqtt.connect(cid.c_str(), nullptr, nullptr, TOPIC_STATUS, 0, true, "offline")) {
    mqtt.subscribe(TOPIC_COMMAND);
    mqtt.publish(TOPIC_STATUS, "online", true);
    Serial.println("[MQTT] Da ket noi broker thanh cong! San sang nhan lenh tu Node-RED.");
  } else {
    Serial.printf("[MQTT] Ket noi that bai (rc=%d), thu lai sau 3s\n", mqtt.state());
  }
}

void publishTelemetry() {
  static uint32_t last = 0;
  if (millis() - last < TELEMETRY_PERIOD_MS) return;
  last = millis();

  bool running = state != ST_IDLE && state != ST_DONE && state != ST_ERROR;
  static uint32_t lastSerialLog = 0;
  if (running && millis() - lastSerialLog >= 1000) {
    lastSerialLog = millis();
    Serial.printf("[BREW] %s | %.1f / %.0f g | flow=%.2f g/s | temp=%.1f C | duty=%d\n",
                  STATE_NAMES[state], weightG, recipe.targetG(), max(flowGps, 0.0f), waterTempC, pumpDuty);
  }

  if (!mqtt.connected()) return;

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
  doc["elapsed_s"] = running ? (millis() - brewStartMs) / 1000 : 0;
  doc["servo_angle"] = servoAngle;
  doc["pump_duty"] = pumpDuty;
  doc["message"] = lastMessage;

  char buf[320];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(TOPIC_TELEMETRY, (const uint8_t*)buf, n, false);
}

// ===================== Nút nhấn & Serial =====================
void handleButtons() {
  static uint32_t lastTare = 0, lastStop = 0;
  if (digitalRead(PIN_BTN_STOP) == LOW && millis() - lastStop > 300) {
    lastStop = millis();
    abortBrew("Dung khan cap (nut nhan)");
  }
  if (digitalRead(PIN_BTN_TARE) == LOW && millis() - lastTare > 300) {
    lastTare = millis();
    if (state == ST_IDLE || state == ST_DONE || state == ST_ERROR) {
      tareScale();
      lastMessage = "Tare";
      Serial.println("[BTN] Tare -> 0.0 g");
    }
  }
}

void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "START") {
    startBrew();
  } else if (cmd == "STOP") {
    abortBrew("Da dung qua Serial");
  } else if (cmd == "TARE") {
    tareScale();
    Serial.println("[SERIAL] Tare -> 0.0 g");
  }
}

// ===================== setup / loop =====================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BTN_TARE, INPUT_PULLUP);
  pinMode(PIN_BTN_STOP, INPUT_PULLUP);

  // Servo 50 Hz, bơm 1 kHz, buzzer 2 kHz -> cần 3 timer LEDC riêng
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  nozzleServo.setPeriodHertz(50);
  nozzleServo.attach(PIN_SERVO, 500, 2400);
  nozzleServo.write(servoAngle);
  pumpPwm.attachPin(PIN_PUMP, 1000, 8);
  setPump(0);
  buzzerPwm.attachPin(PIN_BUZZER, 2000, 10);
  buzzer(false);

  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  scale.set_scale(HX711_CAL_FACTOR);
  tareScale();
#if SIMULATION_MODE
  simLastMs = millis();
  Serial.println("[SIM] SIMULATION_MODE = 1: khoi luong nuoc duoc mo phong theo duty bom");
#endif

  ds18.begin();
  ds18.setWaitForConversion(false);

  Serial.printf("[WiFi] Dang ket noi %s ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  beginWifi();
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 6000) {
    delay(100);
    Serial.print(".");
  }

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);
  mqtt.setBufferSize(512);

  beep(2);
  Serial.println("\nPHY3640 IoT Gravimetric Coffee Brewer (ESP32-S3) - ready");
  Serial.println("-> Bam START tren Node-RED Dashboard (hoac go START vao Serial Monitor) de pha!");
}

void loop() {
  ensureWifi();
  ensureMqtt();
  mqtt.loop();

  readWeight();
  updateFlow();
  updateTemperature();
  handleButtons();
  handleSerial();
  runStateMachine();
  updateBeep();
  publishTelemetry();
  delay(10);  // nhường CPU cho Wi-Fi/TCP stack & giúp Wokwi mô phỏng đạt ~100% tốc độ
}
