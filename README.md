# Monitoring Energi Listrik PZEM-004T + ESP32-C3 Super Mini

Sketch ini dibuat untuk Arduino IDE. Program membaca data dari PZEM-004T 100A memakai ESP32-C3 Super Mini, menampilkan hasil di Serial Monitor/OLED, dan mengirim data ke Home Assistant lewat MQTT.

## Library Arduino IDE

Pasang dari **Tools > Manage Libraries**:

- **PZEM004Tv30** by mandulaj
- **PubSubClient** by Nick O'Leary
- **Adafruit SH110X** by Adafruit
- **Adafruit GFX Library** by Adafruit

Pastikan board ESP32 sudah tersedia di Arduino IDE lewat Boards Manager:

- **esp32** by Espressif Systems

## Wiring

Default pin pada sketch:

### PZEM-004T

| ESP32-C3 Super Mini | PZEM-004T |
| --- | --- |
| GPIO5 TX | RX |
| GPIO4 RX | TX |
| 5V | VCC |
| GND | GND |

### OLED SH1106 I2C 1.3 Inch 128x64

| ESP32-C3 Super Mini | OLED SH1106 |
| --- | --- |
| GPIO6 | SDA |
| GPIO7 | SCK |
| 3V3 | VCC |
| GND | GND |

Pada OLED I2C 4-pin, label `SCK` sama fungsinya dengan `SCL`.

Default OLED di sketch:

- Resolusi: `128x64`
- Alamat I2C: `0x3C`

Jika OLED tidak tampil dan Serial Monitor menulis OLED tidak terdeteksi, coba ubah:

```cpp
static const uint8_t OLED_ADDRESS = 0x3C;
```

menjadi:

```cpp
static const uint8_t OLED_ADDRESS = 0x3D;
```

Catatan penting:

- Beban dan kabel AC harus dipasang dengan hati-hati karena PZEM membaca tegangan listrik PLN.
- Jika pin TX dari PZEM mengeluarkan logika 5V, gunakan level shifter atau pembagi tegangan ke RX ESP32-C3 karena GPIO ESP32-C3 tidak 5V tolerant.
- CT clamp PZEM dipasang pada salah satu kabel saja, biasanya kabel fase/live, bukan fase dan netral sekaligus.

## Cara Upload

1. Buka file `pzem_004t_100A_esp32.ino` di Arduino IDE.
2. Pilih board: **ESP32C3 Dev Module**.
3. Di menu **Tools**, gunakan setting berikut:
   - **USB CDC On Boot: Enabled**
   - **Upload Mode: UART0 / Hardware CDC** jika pilihan ini tersedia
   - **CPU Frequency: 160MHz**
   - **Flash Mode: DIO**
   - **Partition Scheme: Default 4MB with spiffs** atau pilihan lain yang memiliki **OTA**
4. Pilih port ESP32-C3.
5. Upload sketch.
6. Buka Serial Monitor di **115200 baud**.
7. Tekan tombol **RST/Reset** pada ESP32-C3 setelah Serial Monitor terbuka.

## Upload Wireless OTA

Sketch sudah mendukung upload OTA lewat WiFi dengan hostname:

```text
pzem-esp32c3
```

Cara pakai:

1. Upload pertama tetap lewat USB.
2. Saat upload USB pertama, pastikan **Partition Scheme** bukan `Huge APP / No OTA`.
3. Pastikan ESP32-C3 berhasil terhubung ke WiFi.
4. Di Arduino IDE, buka **Tools > Port**.
5. Pilih network port yang muncul sebagai `pzem-esp32c3` atau IP ESP32.
6. Upload sketch seperti biasa.

Jika log compile menampilkan maksimum sekitar `3145728 bytes`, itu biasanya partisi **No OTA**. Ganti partition scheme ke opsi yang memiliki OTA, upload ulang lewat USB, lalu coba OTA lagi.

Jika OTA mulai upload lalu gagal di awal seperti `1%` sampai `10%`, biasanya koneksi OTA terganggu saat firmware masih menjalankan pekerjaan lain. Sketch ini sudah masuk mode khusus saat OTA dimulai: pembacaan PZEM, MQTT reconnect, dan publish data dihentikan sementara sampai OTA selesai.

Password OTA default kosong:

```cpp
const char *OTA_PASSWORD = "";
```

Jika ingin memakai password, isi `OTA_PASSWORD`, upload sekali lewat USB, lalu upload berikutnya bisa lewat OTA.

## Home Assistant MQTT

Sketch ini sudah memakai konfigurasi MQTT berikut:

```cpp
const char *ssid = "Mess 6";
const char *password = "w4k4w4k4";
const char *mqtt1_server = "192.168.110.199";
const int mqtt1_port = 1883;
const char *mqtt1_user = "";
const char *mqtt1_password = "";

const char *mqtt2_server = "192.168.110.49";
const int mqtt2_port = 1883;
const char *mqtt2_user = "";
const char *mqtt2_password = "";
```

Pastikan integrasi **MQTT** di kedua Home Assistant sudah aktif dan broker berada di alamat `192.168.110.199:1883` dan `192.168.110.49:1883`.

Sketch akan mengirim MQTT Discovery otomatis ke kedua broker pada topic:

```text
homeassistant/sensor/pzem_004t_esp32/...
```

Data sensor dikirim ke:

```text
pzem_004t_esp32/state
```

Status online/offline dikirim ke:

```text
pzem_004t_esp32/status
```

Entity yang akan muncul di Home Assistant:

- PZEM Voltage
- PZEM Current
- PZEM Power
- PZEM Energy
- PZEM Frequency
- PZEM Power Factor
- ESP32 Chip Temperature
- Reset PZEM Energy

## Reset Energi

Reset energi tersedia sebagai tombol Home Assistant lewat MQTT Discovery. Command topic tombol:

```text
pzem_004t_esp32/reset_energy/set
```

Payload yang dikirim tombol:

```text
RESET
```

## Jika Data Tidak Terbaca

- Pastikan RX dan TX disilang: TX ESP32 ke RX PZEM, RX ESP32 ke TX PZEM.
- Pastikan GND ESP32 dan GND PZEM tersambung.
- Coba tukar pin di sketch jika board ESP32-C3 Super Mini Anda memakai layout berbeda.
- Pastikan library PZEM004Tv30 sudah terpasang.

## Jika OLED Tidak Tampil

- Pastikan library **Adafruit SH110X** dan **Adafruit GFX Library** sudah terpasang.
- Pastikan wiring I2C benar: SDA ke GPIO6, SCK/SCL ke GPIO7.
- Pastikan OLED memakai VCC 3.3V atau modulnya mendukung 3.3V.
- Coba ganti `OLED_ADDRESS` dari `0x3C` ke `0x3D`.
- Untuk OLED 1.3 inch 128x64, pastikan `OLED_HEIGHT` adalah `64`.
- Sketch ini memakai driver **SH1106** lewat library **Adafruit SH110X**.
- Jika ingin mematikan OLED sementara, ubah `USE_OLED` menjadi `false`.

## Jika OLED Buram atau Tidak Jelas

- Sketch menampilkan voltase, power, dan energi dalam ukuran lebih besar. Header atas menampilkan status WiFi, status MQTT, dan suhu internal chip ESP32-C3 dalam satuan Celcius.
- Pastikan plastik pelindung layar sudah dilepas.
- Pastikan supply stabil. Jika layar redup saat WiFi aktif, coba VCC OLED ke 5V jika modul OLED Anda mendukung 5V.
- Jika tulisan masih tampak geser atau acak, coba ganti `OLED_ADDRESS` dari `0x3C` ke `0x3D`, lalu upload ulang.

## Jika Serial Monitor Kosong

- Pastikan baud Serial Monitor **115200**.
- Untuk ESP32-C3 Super Mini, pastikan **Tools > USB CDC On Boot > Enabled**. Jika disabled, `Serial.print()` bisa keluar ke UART0, bukan ke USB Serial Monitor.
- Setelah upload selesai, tutup lalu buka lagi Serial Monitor, kemudian tekan tombol **RST/Reset**.
- Pastikan port yang dipilih adalah port ESP32-C3 yang muncul setelah board dicolok.
- Jika masih kosong, coba board profile **ESP32C3 Dev Module** dan upload ulang.
