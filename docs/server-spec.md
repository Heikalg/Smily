# Smily IoT — Spesifikasi Server & Dashboard

Dokumen pembagian tugas. **Sisi device (ESP32/firmware) dikerjakan oleh tim IoT.**
Dokumen ini untuk **tim server/dashboard** (XAMPP + PHP + MySQL + AI).

Tujuannya: supaya **kontrak data** (bentuk JSON yang dikirim device) sama persis
di kedua sisi, sehingga device dan server langsung nyambung.

---

## 1. Gambaran Arsitektur

```
            satu jaringan WiFi (router / hotspot yang sama)
ESP32-C3 (Smily) ──POST http://<IP-laptop>/log.php──▶  PC (XAMPP)
  - baca DHT11 (suhu, kelembapan)                        ├─ Apache + PHP
  - ambil cuaca (Open-Meteo)                             └─ MySQL
  - kirim tiap wake / terjadwal

Browser (PC / HP) ──http://<IP-laptop>/dashboard.php──▶  dashboard
                                                          ├─ grafik data historis
                                                          └─ AI (ringkasan / prediksi)
```

**Prinsip penting:**
- Semua komunikasi device↔server pakai **HTTP biasa (port 80)**, BUKAN HTTPS.
  Alasan: ESP32-C3 tidak andal melakukan TLS 1.3 (sudah diuji — gagal `-1`).
  Di jaringan lokal HTTP sudah cukup.
- Device **tidak** bicara MySQL langsung. Device hanya POST ke `log.php`;
  `log.php` yang melakukan `INSERT` ke MySQL.
- **AI jalan di sisi dashboard (PHP/JS), bukan di device dan bukan di MySQL.**
  MySQL hanya menyimpan baris data; dashboard yang mengirim data itu ke API AI.

---

## 2. KONTRAK DATA (paling penting — harus sama di kedua sisi)

Device akan mengirim **HTTP POST** dengan body **JSON** ke endpoint `log.php`.

### Request dari device

```
POST /log.php  HTTP/1.1
Host: <IP-laptop>
Content-Type: application/json

{
  "device": "smily-01",
  "temp": 28.3,
  "humidity": 55,
  "weather_code": 2,
  "is_day": 1
}
```

| Field | Tipe | Arti | Catatan |
|---|---|---|---|
| `device` | string | ID perangkat | untuk membedakan kalau ada >1 device |
| `temp` | float | suhu °C dari DHT11 | bisa `null` kalau sensor gagal baca |
| `humidity` | int | kelembapan % dari DHT11 | bisa `null` |
| `weather_code` | int | kode cuaca WMO (Open-Meteo) | 0=cerah, 2/3=berawan, 61=hujan, 95=badai |
| `is_day` | int | 1=siang, 0=malam | dari Open-Meteo |

> Catatan: waktu (`timestamp`) **diisi oleh server** saat row masuk
> (`NOW()`), BUKAN oleh device. Device tidak punya jam real-time (RTC) yang
> akurat, jadi lebih andal server yang stempel waktu.

### Response yang diharapkan device

Device cukup butuh tahu sukses/gagal. Balas **HTTP 200** dengan body apa pun
(mis. `{"ok":true}`). Kode non-200 dianggap gagal oleh device.

---

## 3. Skema Database MySQL

Nama database usulan: `smily`. Satu tabel cukup:

```sql
CREATE DATABASE IF NOT EXISTS smily
  CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE smily;

CREATE TABLE IF NOT EXISTS readings (
  id            INT AUTO_INCREMENT PRIMARY KEY,
  device        VARCHAR(32)   NOT NULL DEFAULT 'smily-01',
  temp          FLOAT         NULL,          -- °C (NULL bila sensor gagal)
  humidity      INT           NULL,          -- % (NULL bila sensor gagal)
  weather_code  INT           NULL,          -- kode WMO Open-Meteo
  is_day        TINYINT(1)    NULL,          -- 1=siang 0=malam
  created_at    TIMESTAMP     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_created (created_at)
);
```

---

## 4. `log.php` — endpoint penerima data (yang harus dibuat)

Tanggung jawab:
1. Terima POST JSON.
2. Validasi field.
3. `INSERT` ke tabel `readings`.
4. Balas `{"ok":true}` (HTTP 200) atau error (HTTP 4xx/5xx).

Kerangka acuan (silakan dirapikan):

```php
<?php
header('Content-Type: application/json');

$pdo = new PDO('mysql:host=localhost;dbname=smily;charset=utf8mb4', 'root', '');
// XAMPP default: user 'root', password kosong. Sesuaikan bila beda.

$raw  = file_get_contents('php://input');
$data = json_decode($raw, true);
if (!$data) { http_response_code(400); echo '{"ok":false,"err":"bad json"}'; exit; }

$stmt = $pdo->prepare(
  'INSERT INTO readings (device, temp, humidity, weather_code, is_day)
   VALUES (:device, :temp, :humidity, :wcode, :isday)'
);
$stmt->execute([
  ':device'   => $data['device']       ?? 'smily-01',
  ':temp'     => $data['temp']         ?? null,
  ':humidity' => $data['humidity']     ?? null,
  ':wcode'    => $data['weather_code'] ?? null,
  ':isday'    => $data['is_day']       ?? null,
]);

echo '{"ok":true}';
```

> Pakai **prepared statement** (seperti di atas) supaya aman dari SQL injection.

---

## 5. `dashboard.php` — tampilan + AI (yang harus dibuat)

Tanggung jawab:
1. `SELECT` data dari `readings` (mis. 24 jam terakhir).
2. Tampilkan **grafik** garis suhu & kelembapan terhadap waktu (saran: Chart.js
   via CDN — gampang).
3. Panggil **AI** untuk menganalisa data, lalu tampilkan hasilnya.

### Bagian AI — cara kerjanya

AI **tidak** ada di MySQL. Alurnya:
1. Ambil baris data dari MySQL (mis. 50 baris terakhir).
2. Susun jadi teks ringkas.
3. Kirim ke API LLM (saran: **Google Gemini** — ada free tier) dengan prompt,
   misal:
   > "Berikut data suhu & kelembapan ruangan 24 jam terakhir: [data].
   >  Buat ringkasan singkat kondisi, sebutkan tren, dan tandai anomali bila ada."
4. Tampilkan jawaban AI di dashboard.

Butuh **API key** dari Google AI Studio (gratis). Simpan key di sisi server
(jangan di JavaScript yang terlihat publik).

Ide fitur AI (dari paling gampang):
- **Level 1 — Ringkasan teks:** "Hari ini cenderung hangat & lembap, stabil."
- **Level 2 — Deteksi anomali:** "Suhu melonjak 6° dalam 10 menit pukul 14:20."
- **Level 3 — Prediksi tren:** "Kelembapan naik terus → kemungkinan hujan sore."

---

## 6. Yang perlu disepakati antara tim IoT & tim server

| Hal | Nilai | Siapa menentukan |
|---|---|---|
| IP laptop server (`192.168.x.x`) | cek `ipconfig` → IPv4 | tim server, kasih ke tim IoT |
| Port | 80 (default Apache) | tetap |
| Path endpoint | `/log.php` | sepakati |
| Nama DB / tabel | `smily` / `readings` | tim server |
| Field JSON | lihat Bagian 2 | **harus sama persis** |
| Interval kirim device | mis. tiap 15 menit / tiap wake | tim IoT |

> **Checklist jaringan:** laptop & device WAJIB di WiFi yang sama. Firewall
> Windows mungkin perlu meng-allow Apache (port 80) untuk koneksi dari device.

---

## 7. Batasan yang Harus Disadari (jujur)

- **Laptop harus nyala + XAMPP jalan** supaya logging masuk. Kalau laptop
  mati / pindah WiFi, logging berhenti. Wajar untuk demo; bukan 24/7 seperti cloud.
- **IP lokal bisa berubah** kalau router memberi DHCP baru. Kalau device tiba-tiba
  gagal kirim, cek dulu apakah IP laptop berubah.
- **AI butuh internet** (panggil API LLM). Logging-nya murni lokal; hanya bagian
  AI yang perlu koneksi keluar.

---

## 8. Pembagian Tugas

**Tim IoT (device/firmware) — sudah/akan dikerjakan:**
- Baca DHT11, ambil cuaca Open-Meteo.
- Modul `logger` di firmware: POST JSON (Bagian 2) ke `log.php` tiap wake.
- Isi `SMILY_SERVER_HOST` (IP laptop) di `config.h`.

**Tim Server/Dashboard — dokumen ini:**
- Install XAMPP (Apache + PHP + MySQL).
- Buat DB + tabel (Bagian 3).
- Buat `log.php` (Bagian 4).
- Buat `dashboard.php` + grafik + AI (Bagian 5).
- Kasih IP laptop + konfirmasi field JSON ke tim IoT.
