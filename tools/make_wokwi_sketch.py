"""Tạo gói Wokwi Web (wokwi/) từ mã nguồn PlatformIO.

Chạy:  python tools/make_wokwi_sketch.py

- wokwi/sketch.ino   = include/config.h + src/main.cpp (bật SIMULATION_MODE=1)
- wokwi/diagram.json = bản sao diagram.json ở thư mục gốc
- wokwi/libraries.txt = danh sách thư viện theo tên trong Arduino Library Manager

Chỉ sửa code ở src/ và include/, sau đó chạy lại script này; không sửa tay wokwi/sketch.ino.
"""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "wokwi"

LIBRARIES = [
    "HX711 Arduino Library",
    "OneWire",
    "DallasTemperature",
    "ESP32Servo",
    "PubSubClient",
    "ArduinoJson",
]


def main() -> None:
    OUT.mkdir(exist_ok=True)
    config = (ROOT / "include" / "config.h").read_text(encoding="utf-8")
    main_cpp = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")

    config = config.replace("#pragma once\n", "")
    main_cpp = main_cpp.replace('#include "config.h"\n', "")

    sketch = (
        "// =====================================================================\n"
        "// FILE TỰ ĐỘNG SINH bởi tools/make_wokwi_sketch.py - KHÔNG SỬA TAY.\n"
        "// Nguồn: include/config.h + src/main.cpp\n"
        "// =====================================================================\n"
        "#define SIMULATION_MODE 1  // Wokwi: mô phỏng khối lượng nước theo duty bơm\n\n"
        "// ---------------- include/config.h ----------------\n"
        f"{config}\n"
        "// ---------------- src/main.cpp ----------------\n"
        f"{main_cpp}"
    )
    (OUT / "sketch.ino").write_text(sketch, encoding="utf-8", newline="\n")
    shutil.copyfile(ROOT / "diagram.json", OUT / "diagram.json")
    (OUT / "libraries.txt").write_text(
        "# Wokwi Library Manager\n" + "\n".join(LIBRARIES) + "\n", encoding="utf-8", newline="\n"
    )
    print("Đã tạo wokwi/sketch.ino, wokwi/diagram.json, wokwi/libraries.txt")


if __name__ == "__main__":
    main()
