# ☀️ HELIOTROPS – Smart Solar Light Tracker

**Sistem pelacak cahaya matahari berbasis Internet of Things (IoT) menggunakan ESP32 untuk mengoptimalkan posisi panel surya berdasarkan intensitas cahaya.**

## 📌 Tentang Proyek

HELIOTROPS (Smart Solar Light Tracker) merupakan proyek berbasis Internet of Things (IoT) yang dirancang untuk menggerakkan panel surya secara otomatis mengikuti arah datangnya cahaya dengan intensitas paling tinggi.

Sistem ini menggunakan empat sensor LDR untuk mendeteksi intensitas cahaya dari empat arah, yaitu atas, bawah, kiri, dan kanan. Data dari sensor diproses oleh ESP32 untuk mengendalikan dua motor stepper yang mengatur pergerakan panel surya secara horizontal dan vertikal.

Selain melakukan pelacakan cahaya, sistem ini juga dilengkapi sensor INA219 untuk memantau parameter kelistrikan panel surya. Seluruh data pengukuran dikirim menggunakan protokol MQTT, disimpan ke dalam database SQLite, dan ditampilkan melalui dashboard berbasis Streamlit.

## ✨ Fitur Utama

- **Automatic Solar Tracking** – Menggerakkan panel surya secara otomatis mengikuti arah cahaya dengan intensitas tertinggi.
- **Four-Direction Light Detection** – Mendeteksi intensitas cahaya menggunakan empat sensor LDR.
- **Dual-Axis Movement** – Mengatur pergerakan panel surya secara horizontal dan vertikal menggunakan dua motor stepper.
- **Electrical Monitoring** – Memantau tegangan, arus, daya, dan estimasi energi menggunakan sensor INA219.
- **LCD Monitoring** – Menampilkan informasi sistem melalui LCD 16×2 I2C.
- **Real-Time Dashboard** – Menampilkan data sensor dan posisi panel surya melalui dashboard Streamlit.
- **Remote Control** – Mengaktifkan dan menghentikan sistem tracking melalui dashboard.
- **Data Logging** – Menyimpan hasil pengukuran ke dalam database SQLite.
- **MQTT Communication** – Menghubungkan ESP32 dengan server dan dashboard menggunakan protokol MQTT.
- **Local Tracking** – Sistem tracking tetap berjalan secara lokal pada ESP32 meskipun koneksi internet atau MQTT terputus.

## 🛠️ Komponen Hardware

| Komponen | Fungsi |
|---|---|
| ESP32 DevKit | Mikrokontroler utama |
| Solar Panel 5V 220mA 1.1W | Mengubah energi cahaya menjadi energi listrik |
| 4× LDR | Mendeteksi intensitas cahaya |
| INA219 | Mengukur tegangan dan arus |
| 2× Stepper Motor 28BYJ-48 | Menggerakkan panel surya |
| 2× ULN2003 Driver | Mengendalikan motor stepper |
| LCD 16×2 I2C | Menampilkan informasi sistem |
| Push Button | Kontrol manual |
| 18650 Battery Shield / UPS | Modul catu daya |

## 💻 Teknologi yang Digunakan

**Hardware & Embedded System**
- ESP32
- Arduino IDE
- C++ (Arduino Framework)

**Backend & Database**
- Python
- MQTT
- Mosquitto Broker
- SQLite
- Paho MQTT

**Dashboard & Visualisasi**
- Streamlit
- Pandas
- Plotly

## 🏗️ Arsitektur Sistem

```text
       4× LDR Sensor       INA219
             │               │
             └───────┬───────┘
                     │
                   ESP32
                     │
           ┌─────────┴─────────┐
           │                   │
      LCD 16×2 I2C       2× Stepper Motor
                               │
                        Solar Tracking
                     │
                     ▼
                MQTT Broker
                     │
                     ▼
          Python MQTT Subscriber
                     │
                     ▼
                SQLite DB
                     │
                     ▼
           Streamlit Dashboard
                     │
              Monitoring &
              Remote Control
                     │
                     └──── MQTT Command ────► ESP32
```

## 📂 Struktur Folder Proyek

```text
HELIOTROPS/
│
├── cek_rangkaian_only/
│   ├── cek_rangkaian_only.ino
│   └── baca.txt
│
├── server/
│   ├── create_db.py
│   ├── database.py
│   └── mqtt_subscriber.py
│
├── database/
│   └── heliotrops.db
│
├── dashboard/
│   └── app.py
│
└── heliotrops_esp32/
    └── HELIOTROPS_integrated_revised.ino
```

### Keterangan Folder

| Folder | Keterangan |
|---|---|
| `cek_rangkaian_only` | Berisi kode khusus untuk mengecek dan menguji rangkaian hardware ESP32 |
| `server` | Berisi program Python untuk inisialisasi database dan penerimaan data MQTT |
| `database` | Menyimpan database SQLite hasil pengukuran sensor |
| `dashboard` | Berisi aplikasi dashboard monitoring dan kontrol berbasis Streamlit |
| `heliotrops_esp32` | Berisi firmware utama ESP32 untuk menjalankan sistem HELIOTROPS |

### ⚠️ Catatan Folder `cek_rangkaian_only`

Folder `cek_rangkaian_only` merupakan program terpisah yang hanya digunakan untuk melakukan pengujian rangkaian hardware.

Program ini dapat digunakan untuk memeriksa koneksi komponen, sensor, LCD, dan motor sebelum menjalankan firmware utama.

**Program ini tidak menggunakan:**
- MQTT Broker
- Python Server
- Database SQLite
- Streamlit Dashboard
- Koneksi ke sistem backend

Gunakan program ini hanya untuk keperluan pengecekan rangkaian, bukan sebagai firmware utama sistem HELIOTROPS.

## ⚙️ Instalasi dan Konfigurasi

### 1. Clone Repository

```bash
git clone https://github.com/your-username/HELIOTROPS.git
cd HELIOTROPS
```

### 2. Instalasi Library Python

Pastikan Python sudah terinstal, kemudian jalankan:

```bash
pip install streamlit pandas plotly paho-mqtt
```

### 3. Instalasi MQTT Broker

Instal Mosquitto MQTT Broker pada komputer yang digunakan sebagai server.

Pastikan broker berjalan pada port `1883`.

Konfigurasi koneksi MQTT pada aplikasi Python:

```python
MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
```

Sesuaikan alamat IP MQTT Broker pada firmware ESP32 dengan alamat IP lokal komputer server.

### 4. Inisialisasi Database

Jalankan perintah berikut dari direktori utama proyek:

```bash
python server/create_db.py
```

Perintah ini akan membuat database SQLite beserta tabel yang dibutuhkan apabila belum tersedia.

### 5. Konfigurasi ESP32

Buka firmware utama:

```text
heliotrops_esp32/HELIOTROPS_integrated_revised.ino
```

Sesuaikan:
- Nama Wi-Fi.
- Password Wi-Fi.
- Alamat IP MQTT Broker.

Pastikan library Arduino yang diperlukan sudah terinstal, kemudian unggah program ke ESP32.

### 6. Jalankan MQTT Subscriber

Buka terminal dari direktori utama proyek:

```bash
python server/mqtt_subscriber.py
```

Program ini bertugas menerima data sensor dari ESP32 melalui MQTT dan menyimpannya ke database SQLite.

### 7. Jalankan Dashboard

Buka terminal baru dan jalankan:

```bash
streamlit run dashboard/app.py
```

Dashboard akan terbuka melalui browser dan menampilkan informasi monitoring sistem HELIOTROPS.

## 📡 MQTT Communication

HELIOTROPS menggunakan dua topik MQTT utama:

| Topic | Arah Komunikasi | Fungsi |
|---|---|---|
| `heliotrops/sensor_data` | ESP32 → Server | Mengirim data hasil pembacaan sensor |
| `heliotrops/control` | Dashboard → ESP32 | Mengirim perintah ON/OFF tracking |

## 📊 Informasi Monitoring Dashboard

Dashboard HELIOTROPS menyediakan beberapa informasi utama:

1. **Intensitas Cahaya** – Rata-rata pembacaan empat sensor LDR.
2. **Arah Cahaya** – Estimasi arah berdasarkan posisi azimuth.
3. **Azimuth** – Posisi sudut horizontal panel.
4. **Elevation** – Posisi sudut vertikal panel.
5. **Voltage Panel** – Tegangan panel surya dalam Volt.
6. **Current Panel** – Arus panel surya dalam mA.
7. **Grafik Intensitas Cahaya** – Visualisasi perubahan intensitas cahaya.
8. **Riwayat Database** – Menampilkan data pengukuran yang tersimpan.
9. **Download CSV** – Mengunduh data riwayat pengukuran.
10. **Kontrol Tracker** – Mengaktifkan atau menghentikan tracking melalui dashboard.

## 🔄 Alur Kerja Sistem

1. Empat sensor LDR membaca intensitas cahaya dari berbagai arah.
2. ESP32 memproses nilai pembacaan sensor.
3. Dua motor stepper mengatur posisi panel surya berdasarkan perbedaan intensitas cahaya.
4. INA219 membaca parameter kelistrikan panel surya.
5. ESP32 mengirimkan data pengukuran melalui MQTT ketika koneksi tersedia.
6. MQTT Subscriber menerima data dari ESP32.
7. Data pengukuran disimpan ke dalam database SQLite.
8. Dashboard Streamlit mengambil data dari database untuk ditampilkan.
9. Pengguna dapat mengirim perintah ON/OFF melalui dashboard.

**Catatan:** Tracking tetap berjalan secara lokal pada ESP32 ketika koneksi MQTT terputus. Namun, data selama koneksi terputus tidak disimpan dalam antrean untuk dikirim ulang setelah koneksi pulih.

## 🎯 Tujuan Proyek

Proyek HELIOTROPS dikembangkan sebagai implementasi teknologi Internet of Things (IoT) dalam sistem pelacakan cahaya matahari otomatis.

Melalui proyek ini, beberapa konsep teknologi yang diterapkan meliputi:

- Pemrograman mikrokontroler ESP32.
- Integrasi sensor dan aktuator.
- Sistem otomasi berbasis sensor.
- Komunikasi IoT menggunakan MQTT.
- Pengelolaan database menggunakan SQLite.
- Pengembangan backend menggunakan Python.
- Visualisasi data menggunakan Streamlit dan Plotly.

## 👨‍💻 Pengembang

**1. Grahito Ardianto Purnomo (123240001)**
**2. Fahri Hidayatullah (123240002)**
**3. Krocasadigandra Arzaq Kubro Muhammad (123240015)**
**4. Hardanu Ilham Purnama (123240166)**

Mahasiswa Informatika  
Universitas Pembangunan Nasional "Veteran" Yogyakarta

---

**HELIOTROPS – Smart Solar Light Tracker**

*An IoT-Based Automatic Solar Tracking System.*
