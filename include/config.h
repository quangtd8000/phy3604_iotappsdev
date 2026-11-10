#pragma once
// ===================== Cấu hình hệ thống =====================

// ---- Wi-Fi (Wokwi dùng mạng "Wokwi-GUEST"; đổi sang Wi-Fi thật khi ráp mạch) ----
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASS ""

// ---- MQTT ----
#define MQTT_HOST "broker.hivemq.com"
#define MQTT_PORT 1883
#define TOPIC_TELEMETRY "phy3640/quang/coffee/telemetry"
#define TOPIC_COMMAND   "phy3640/quang/coffee/command"
#define TOPIC_STATUS    "phy3640/quang/coffee/status"
#define TELEMETRY_PERIOD_MS 250

// ---- Chân GPIO (ESP32-S3) ----
#define PIN_HX711_DT   16
#define PIN_HX711_SCK  17
#define PIN_DS18B20     4
#define PIN_PUMP        6   // PWM -> MOSFET -> bơm
#define PIN_SERVO       5   // PWM -> Servo đảo vòi
#define PIN_BUZZER      7   // Còi Buzzer báo hiệu (phát tone PWM 2 kHz)
#define PIN_LED        15   // LED chỉ báo trạng thái
#define PIN_BTN_TARE    1   // nút Tare (nối GND, INPUT_PULLUP)
#define PIN_BTN_STOP    2   // nút dừng khẩn cấp (nối GND, INPUT_PULLUP)

// ---- Loadcell ----
// Wokwi: ~420.0983. Với loadcell thật: hiệu chuẩn bằng vật chuẩn (xem README mục Hiệu chuẩn).
#define HX711_CAL_FACTOR 420.0983f
#define EMA_ALPHA        0.30f   // bộ lọc Exponential Moving Average
#define FLOW_WINDOW      6       // số mẫu (mỗi 200ms) để tính đạo hàm lưu lượng
#define FLOW_SAMPLE_MS   200

// ---- Chế độ mô phỏng ----
// 1 = khối lượng nước được mô phỏng theo duty bơm (dùng cho Wokwi / demo, không cần kéo thanh trượt HX711)
// 0 = đọc loadcell thật qua HX711
#ifndef SIMULATION_MODE
#define SIMULATION_MODE 0
#endif
#define SIM_MAX_FLOW_GPS   5.0f   // lưu lượng bơm mô phỏng khi duty = 255
#define SIM_DEAD_DUTY      40     // dưới mức duty này bơm chưa đẩy được nước
#define SIM_PUMP_TAU_S     0.4f   // hằng số thời gian quán tính của bơm + ống
#define SIM_NOISE_G        0.3f   // nhiễu do giọt nước rơi (±g)

// ---- Điều khiển bơm ----
#define TARGET_FLOW_GPS  2.5f    // tốc độ rót mong muốn (g/s)
#define FLOW_KI          12.0f   // hệ số bộ điều khiển dạng gia số: duty += K * sai số (duty / (g/s) mỗi chu kỳ 200 ms)
#define PUMP_DUTY_START  150
#define PUMP_DUTY_MIN    70
#define PUMP_DUTY_MAX    255
#define SLOWDOWN_ZONE_G  15.0f   // còn lại < 15g thì giảm tốc
#define STOP_OFFSET_G    1.0f    // dừng sớm để bù lượng nước còn trong ống
#define NO_FLOW_TIMEOUT_MS 15000 // bơm chạy 15s mà khối lượng không tăng -> lỗi (hết nước/không có cốc)
#define DRAWDOWN_TIMEOUT_MS 120000
#define DRAWDOWN_STABLE_MS  3000
#define DRAWDOWN_FLOW_GPS   0.15f

// ---- Servo đảo vòi ----
#define SERVO_MIN_DEG 20
#define SERVO_MAX_DEG 70
#define SERVO_STEP_MS 60

// ---- Nhiệt độ nước ----
#define TEMP_MIN_C 88.0f
#define TEMP_MAX_C 94.0f
