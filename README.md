# ESP32 Space Shooter Console ("Perang Angkasa")

Game arcade *Space Shooter* berbasis mikrokontroler **ESP32** menggunakan layar OLED 128x64, 3 tombol kontrol, 2 LED indikator status, dan buzzer alarm dengan tata letak **full pin sisi kiri**[cite: 2].

## Fitur
- Layar OLED SSD1306 128x64 (~40 FPS)[cite: 2]
- 2 jenis rintangan: Pesawat Musuh (bisa menembak) dan Meteor[cite: 2]
- Sistem nyawa (3 HP), skor, *high score*, dan *invulnerability* sesaat[cite: 2]
- Efek suara *non-blocking* (tembak, ledakan, kena hit, start, dan game over)[cite: 2]
- Indikator LED otomatis (Hijau saat bermain, Merah saat Game Over)[cite: 2]

## Mapping Pin ESP32 (Sisi Kiri)

| Komponen | Pin Komponen | Pin ESP32 | Keterangan |
| :--- | :--- | :--- | :--- |
| **OLED SSD1306 (0x3C)** | VCC | `VIN` (5V) | Power |
| | GND | `GND` | Ground |
| | SDA | `D27` (GPIO 27) | I2C Data |
| | SCL | `D25` (GPIO 25) | I2C Clock |
| **Push Button** | Kiri | `D33` (GPIO 33) | `INPUT_PULLUP` ke GND |
| | Kanan | `D32` (GPIO 32) | `INPUT_PULLUP` ke GND |
| | Tembak / Start | `D14` (GPIO 14) | `INPUT_PULLUP` ke GND |
| **LED Indikator** | LED Hijau | `D13` (GPIO 13) | Seri resistor 220–330Ω ke GND |
| | LED Merah | `D12` (GPIO 12) | Seri resistor 220–330Ω ke GND |
| **Buzzer** | Positif (+) | `D26` (GPIO 26) | Negatif (-) ke GND |

## Library yang Dibutuhkan (Arduino IDE)
- `Wire` (bawaan)[cite: 2]
- `Adafruit_GFX`[cite: 2]
- `Adafruit_SSD1306`[cite: 2]
