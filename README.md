# PHY3640 - IoT Gravimetric Coffee Brewer
**Hệ thống Pha Cà Phê Phin & Pour-Over Tự Động Kiểm Soát Theo Trọng Lượng Thực Tế**

> Đồ án cuối kỳ môn **PHY3640 – IoT Apps Dev (Fall 2026)** · ESP32-S3 · PlatformIO/Arduino · MQTT · Node-RED Dashboard · Wokwi

---

## 1. Đặt bài toán & Cảm hứng thực tế (Problem Statement & Motivation)

### Nhu cầu thực tế
Trong quá trình học tập và làm việc kỹ thuật vào ban đêm, việc pha một ly cà phê Phin truyền thống hoặc Pour-Over thường gặp hai vấn đề lớn:
1. **Sai lệch tỷ lệ chiết xuất (Brew Ratio):** Các máy pha cà phê dân dụng giá rẻ chỉ bơm nước dựa trên thời gian cố định (Timed Dosing). Khi bột cà phê nén chặt hoặc mịn hơn, nước chảy chậm làm lệch hoàn toàn tỷ lệ vàng (thường là `1:12` cho Phin đậm hoặc `1:15` cho Pour-Over), khiến cà phê lúc quá đắng gắt, lúc lại quá nhạt.
2. **Bỏ qua giai đoạn ủ (Blooming Phase):** Cà phê rang xay cần được rót một lượng nước nhỏ (khoảng 2 đến 2.5 lần khối lượng bột) và nghỉ 30–40 giây để giải phóng khí $CO_2$ trước khi rót phần nước chính. Rót tay thủ công rất dễ quên căn giờ hoặc rót quá tay.

### Tính sáng tạo & Cảm hứng từ Hackaday
Lấy cảm hứng từ các cân cà phê thông minh đắt đỏ (như Acaia Lunar) và các dự án máy pha tự động mã nguồn mở trên **Hackaday.io**, dự án này xây dựng một **hệ thống điều khiển vòng kín theo trọng lượng (Closed-Loop Gravimetric Control)**:
- Thay vì đếm giây mù quáng, **ESP32-S3** đọc khối lượng nước thực tế chảy vào phin/cốc theo thời gian thực qua cảm biến lực **Loadcell + HX711** để tính **tốc độ dòng chảy tức thời ($\text{Flow Rate} = \Delta m / \Delta t$, đơn vị $\text{g/s}$)**.
- Tự động điều chỉnh xung **PWM cho bơm nhu động/bơm mini** và góc quét của **động cơ Servo** để phân bổ nước đều trên bề mặt bột cà phê.
- Đồng bộ toàn bộ **Đường cong chiết xuất (Extraction Curve)** lên **Node-RED Dashboard** qua giao thức **MQTT**, cho phép chọn công thức pha (Phin truyền thống / V60 Pour-Over) và kích hoạt pha từ điện thoại thông minh.

### Bài toán vật lý
- **Loadcell (strain gauge) + cầu Wheatstone:** lực làm thanh nhôm biến dạng → điện trở strain gauge thay đổi → cầu Wheatstone cho ra điện áp vi sai cỡ mV → **HX711** (ADC 24-bit, khuếch đại PGA 128) → ESP32-S3.
- **Lưu lượng khối lượng:** $\dot m = \dfrac{m(t) - m(t-\Delta t)}{\Delta t}$ (g/s), tính trên cửa sổ trượt 6 mẫu (~1 s) sau bộ lọc EMA.
- **Điều khiển vòng kín:** bộ điều khiển P điều chỉnh duty PWM của bơm để lưu lượng bám theo giá trị đặt $2.5\ \text{g/s}$, giảm tốc khi còn < 15 g để dừng chính xác tại khối lượng mục tiêu.
- **Nhiệt độ:** DS18B20 giám sát nước trong khoảng $88^\circ\text{C} - 94^\circ\text{C}$.

---

## 2. Nội dung chính & Tính năng hệ thống (Key Features)

1. **Cân tự động & Trừ bì thông minh (Auto-Tare):** Tự động về 0g khi đặt cốc/phin lên bàn cân trước khi bắt đầu chu trình.
2. **Chu trình pha 3 giai đoạn (3-Stage Brewing State Machine):**
   - **Stage 1 – Pre-infusion (Ủ cà phê):** Bơm chính xác lượng nước ủ (ví dụ: 30g nước cho 15g cà phê), dừng bơm và đếm ngược 35 giây để thoát khí $CO_2$.
   - **Stage 2 – Active Extraction (Chiết xuất kiểm soát lưu lượng):** Bơm phần nước còn lại đến đúng khối lượng mục tiêu (ví dụ: 225g) trong khi Servo quay đảo vòi rót theo góc xoắn ốc, duy trì tốc độ rót ổn định $2.0 - 3.0\text{ g/s}$.
   - **Stage 3 – Drawdown & Alert (Róc nước & Báo hiệu):** Theo dõi tốc độ nhỏ giọt cuối chu trình; phát âm báo qua **Loa/Buzzer** và gửi thông báo lên Dashboard khi ly cà phê hoàn tất.
3. **Giám sát nhiệt độ nước (Thermal Interlock):** Đọc nhiệt độ bình nước qua cảm biến **DS18B20**; cảnh báo trên Dashboard nếu nước chưa đạt ngưỡng chiết xuất tiêu chuẩn ($88^\circ\text{C} - 94^\circ\text{C}$).
4. **Điều khiển & Giám sát qua MQTT + Node-RED Dashboard:**
   - Chọn cấu hình pha (Profile: *Vietnamese Phin 1:10*, *Classic Pour-Over 1:15*, *Strong Night-Shift 1:12*).
   - Đồ thị thời gian thực hiển thị song song **Tổng khối lượng (g)** và **Tốc độ rót (g/s)**.
5. **An toàn:** dừng bơm khi 15 s không thấy khối lượng tăng (hết nước / lệch cốc), khi vượt khối lượng mục tiêu +20 g, hoặc khi bấm nút **STOP** / gửi lệnh `STOP`.

---

## 3. Sơ đồ khối hệ thống (System Block Diagram)

```mermaid
graph TB
    subgraph "Khối Cảm Biến (Sensors)"
        LC[Loadcell 1kg + ADC HX711<br/>Đo khối lượng & Lưu lượng g/s]
        TEMP[Cảm biến DS18B20<br/>Đo nhiệt độ nước đầu vào]
        BTN[Nút nhấn vật lý<br/>Tare / Start-Stop khẩn cấp]
    end

    subgraph "Khối Xử Lý Trung Tâm (Controller)"
        ESP[ESP32-S3 DevKitC-1<br/>Firmware: PlatformIO / Arduino Framework<br/>State Machine + Digital Filter + MQTT Client]
    end

    subgraph "Khối Thực Thi (Actuators)"
        PUMP[Bơm nước Mini 5V-12V<br/>Điều tốc PWM qua MOSFET]
        SERVO[Động cơ Servo SG90/MG90S<br/>Quét góc vòi nước đều mặt phin]
        AUDIO[Loa Buzzer & LED trạng thái<br/>Báo hiệu trạng thái pha]
        TFT[Màn hình hiển thị tại chỗ<br/>Hiển thị Gram, Nhiệt độ, Timer]
    end

    subgraph "Tầng Giao Tiếp & Ứng Dụng (IoT Cloud)"
        BROKER((MQTT Broker Online<br/>HiveMQ / EMQX))
        NODERED[Node-RED Dashboard<br/>Web & Mobile UI<br/>Biểu đồ Extraction Curve & Chọn Recipe]
    end

    LC -->|DOUT / SCK| ESP
    TEMP -->|1-Wire GPIO| ESP
    BTN -->|GPIO Interrupt| ESP

    ESP -->|PWM Signal| PUMP
    ESP -->|PWM 50Hz| SERVO
    ESP -->|GPIO / Tone| AUDIO
    ESP -->|SPI / I2C| TFT

    ESP <==>|"Wi-Fi / MQTT (JSON)"| BROKER
    BROKER <==>|"Pub / Sub"| NODERED
```

### Máy trạng thái (Firmware State Machine)

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> TARING: START_BREW
    TARING --> BLOOM_POUR: tare xong
    BLOOM_POUR --> BLOOM_REST: đạt bloom_water_g
    BLOOM_REST --> POURING: hết bloom_time_s
    POURING --> DRAWDOWN: đạt target_g
    DRAWDOWN --> DONE: hết nhỏ giọt, buzzer báo xong
    DONE --> TARING: START_BREW
    BLOOM_POUR --> ERROR: không có dòng chảy
    POURING --> ERROR: không có dòng chảy / quá khối lượng
    BLOOM_POUR --> IDLE: STOP
    BLOOM_REST --> IDLE: STOP
    POURING --> IDLE: STOP
```

---

## 4. Danh sách phần cứng & Sơ đồ kết nối (Hardware & Pinout)

| Thành phần | Linh kiện cụ thể | Chân kết nối ESP32-S3 | Vai trò trong hệ thống |
| :--- | :--- | :--- | :--- |
| **Vi điều khiển** | ESP32-S3 DevKitC-1 (Xtensa dual-core LX7 240MHz) | — | Xử lý thuật toán cân, State Machine và kết nối Wi-Fi/MQTT |
| **Cảm biến trọng lượng** | Loadcell 1kg + Module HX711 | `GPIO 16` (DT), `GPIO 17` (SCK) | Đo khối lượng nước (độ phân giải 0.1g) và tính tốc độ chảy |
| **Cảm biến nhiệt độ** | DS18B20 (Đầu dò chống nước) | `GPIO 4` (1-Wire, trở kéo 4.7kΩ) | Kiểm tra nhiệt độ nước trước và trong khi pha |
| **Cơ cấu bơm** | Bơm nước mini DC + Module MOSFET | `GPIO 6` (PWM 1kHz) | Bơm nước định lượng theo chu trình ủ và chiết xuất |
| **Cơ cấu đảo vòi** | Động cơ Servo MG90S | `GPIO 5` (PWM 50Hz) | Xoay vòi nước phân bổ đều lên bề mặt bột cà phê |
| **Âm thanh & Đèn** | Buzzer + LED trạng thái | `GPIO 7` (Buzzer), `GPIO 15` (LED) | Phát âm báo khi hoàn tất giai đoạn ủ và khi pha xong |
| **Nút nhấn vật lý** | 2 nút nhấn (nối GND, `INPUT_PULLUP`) | `GPIO 1` (Tare), `GPIO 2` (Stop) | Trừ bì thủ công / dừng khẩn cấp |
| **Hiển thị tại chỗ** | Màn hình TFT 128x128 / OLED | `GPIO 8` (SDA), `GPIO 9` (SCL) | Hiển thị khối lượng, thời gian và trạng thái ngay trên máy *(mở rộng I2C/SPI)* |
| **Khung cơ khí** | Vỏ & Giá đỡ in 3D (PLA/PETG) | — | Cách nhiệt bàn cân Loadcell và cố định trục vòi Servo |

![Wiring diagram](assets/wiring_diagram.svg)

### Ảnh chụp phần cứng dự kiến & Mô phỏng Wokwi
<!-- Thêm ảnh thật vào thư mục assets/ rồi bỏ comment các dòng dưới:
![Hardware Components](assets/hardware_preview.jpg)
![Wokwi Simulation](assets/wokwi_simulation.png)
-->
*(Ảnh linh kiện thực tế và ảnh chụp mô phỏng Wokwi sẽ được cập nhật trong thư mục [`assets/`](assets/). Sơ đồ mạch Wokwi có sẵn trong [`diagram.json`](diagram.json).)*

---

## 5. Thiết kế Giao thức MQTT (MQTT Topic Architecture)

- **MQTT Broker:** `broker.hivemq.com` (hoặc EMQX Cloud cá nhân), Port `1883` (TCP) / `8883` (TLS).
- **Topic Publish (ESP32-S3 gửi dữ liệu lên Node-RED mỗi 250ms khi đang pha):**
  - Topic: `phy3640/quang/coffee/telemetry`
  ```json
  {
    "state": "POURING",
    "weight_g": 112.4,
    "target_g": 225.0,
    "flow_rate_gps": 2.4,
    "water_temp_c": 91.5,
    "elapsed_s": 58,
    "servo_angle": 45
  }
  ```
  *(Firmware gửi thêm các trường `recipe`, `temp_ok`, `pump_duty`, `message`.)*
- **Topic Subscribe (Node-RED gửi lệnh cấu hình và điều khiển xuống ESP32-S3):**
  - Topic: `phy3640/quang/coffee/command`
  ```json
  {
    "action": "START_BREW",
    "recipe_name": "Phin_Dam_Da",
    "coffee_dose_g": 20.0,
    "ratio": 10,
    "bloom_water_g": 40.0,
    "bloom_time_s": 35
  }
  ```
  Các `action` khác: `STOP`, `TARE`.
- **Topic trạng thái thiết bị:** `phy3640/quang/coffee/status` (`online` / `offline` – Last Will, retained).

---

## 6. Cấu trúc repo & Cách chạy thử

```
├── platformio.ini          # PlatformIO (ESP32-S3 DevKitC-1 + thư viện)
├── include/config.h        # Cấu hình GPIO (ESP32-S3), Wi-Fi, MQTT
├── src/main.cpp            # Firmware: state machine, EMA, flow rate, MQTT
├── diagram.json            # Sơ đồ mạch Wokwi (board-esp32-s3-devkitc-1)
├── wokwi.toml              # Cấu hình nạp firmware Wokwi (VS Code)
├── libraries.txt           # Thư viện cho Wokwi
├── wokwi/                  # Gói chạy độc lập trên Wokwi Web (sketch.ino, diagram.json, libraries.txt)
├── nodered/flows.json      # Flow Node-RED + Dashboard 2.0
└── assets/                 # Sơ đồ đấu dây, ảnh phần cứng, ảnh Wokwi
```

1. **Build firmware:** `pio run` (hoặc mở thư mục bằng VS Code + PlatformIO).
2. **Mô phỏng Wokwi:** 
   - **Cách 1 (Wokwi Web - Không cần cài đặt):** Mở [wokwi.com/projects/new/esp32-s3](https://wokwi.com/projects/new/esp32-s3), copy file [`wokwi/sketch.ino`](wokwi/sketch.ino) vào code editor, copy [`wokwi/diagram.json`](wokwi/diagram.json) vào tab *diagram.json*, và thêm các thư viện trong [`wokwi/libraries.txt`](wokwi/libraries.txt) vào tab *libraries.txt* → bấm nút **Play ▶**.
   - **Cách 2 (VS Code Extension):** Cài extension *Wokwi for VS Code*, chạy `pio run`, mở `diagram.json` → *Start Simulation*.
   - *Thao tác mô phỏng:* Kéo thanh trượt gram của cảm biến HX711 để mô phỏng nước rót vào cốc; LED `PUMP` đổi độ sáng theo duty PWM; Servo quay quét góc vòi rót; còi Buzzer phát âm báo khi chuyển giai đoạn.
3. **Node-RED:** mở Dashboard tại `http://localhost:1880/dashboard/coffee` (hoặc import [`nodered/flows.json`](nodered/flows.json)).
4. **Đổi topic:** nếu dùng broker công cộng, nên đổi `quang` trong topic thành chuỗi riêng để tránh trùng với người khác (sửa ở `include/config.h` và flow Node-RED).

### Hiệu chuẩn Loadcell
1. Đặt `HX711_CAL_FACTOR = 1.0`, nạp firmware, ghi lại giá trị thô khi đặt vật chuẩn đã biết khối lượng (ví dụ cân 100 g).
2. `calibration_factor = giá_trị_thô / khối_lượng_chuẩn(g)`, cập nhật vào `include/config.h`.
3. Kiểm tra lại với nhiều khối lượng khác nhau và chỉnh `EMA_ALPHA` nếu đồ thị bị nhiễu.

---

## 7. Ba mẹo kỹ thuật áp dụng trong dự án

1. **Mô phỏng 100% trên Wokwi trước khi ráp mạch thật** – ESP32-S3, HX711 (có thanh trượt chỉnh gram), DS18B20, Servo, Buzzer đều có sẵn.
2. **Chống nhiễu Loadcell khi nước rơi:** firmware dùng bộ lọc **EMA** (`EMA_ALPHA = 0.3`) kết hợp cửa sổ trượt 6 mẫu để lấy đạo hàm lưu lượng $\dot m = \frac{m(t)-m(t-\Delta t)}{\Delta t}$, giúp đường cong trên Node-RED mượt như cân thương mại.
3. **Khung in 3D trên Anycubic Kobra 2 Neo – chống trôi điểm 0 do nhiệt (thermal drift):** thêm tấm đế cách nhiệt (PETG/PLA) có khe thoáng khí khoảng 5 mm giữa đáy cốc và bề mặt bắt vít của Loadcell, tránh cốc nóng truyền nhiệt trực tiếp vào thanh nhôm.

---

## 8. Tài liệu tham khảo (References)

1. [Hackaday.io – Open Source IoT Pour-Over Coffee Machines & Smart Scales](https://hackaday.com/?s=pour+over+coffee)
2. Avia Semiconductor, *HX711 24-Bit Analog-to-Digital Converter (ADC) for Weigh Scales Datasheet*.
3. Maxim Integrated, *DS18B20 Programmable Resolution 1-Wire Digital Thermometer Datasheet*.
4. [PlatformIO Documentation – Espressif 32 Platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)
5. [Node-RED Dashboard Documentation](https://flows.nodered.org/node/node-red-dashboard)
