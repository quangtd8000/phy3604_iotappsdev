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

// ---- Chân GPIO ----
#define PIN_HX711_DT   16
#define PIN_HX711_SCK  17
#define PIN_DS18B20     4
#define PIN_PUMP       25   // PWM -> MOSFET -> bơm
#define PIN_SERVO      26
#define PIN_BUZZER     27
#define PIN_LED         2
#define PIN_BTN_TARE   32   // nút Tare (nối GND, INPUT_PULLUP)
#define PIN_BTN_STOP   33   // nút dừng khẩn cấp (nối GND, INPUT_PULLUP)

// ---- Loadcell ----
// Wokwi: ~420.0983. Với loadcell thật: hiệu chuẩn bằng vật chuẩn (xem README mục Hiệu chuẩn).
#define HX711_CAL_FACTOR 420.0983f
#define EMA_ALPHA        0.30f   // bộ lọc Exponential Moving Average
#define FLOW_WINDOW      6       // số mẫu (mỗi 200ms) để tính đạo hàm lưu lượng
#define FLOW_SAMPLE_MS   200

// ---- Điều khiển bơm ----
#define TARGET_FLOW_GPS  2.5f    // tốc độ rót mong muốn (g/s)
#define FLOW_KP          12.0f   // hệ số P (duty / (g/s))
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
