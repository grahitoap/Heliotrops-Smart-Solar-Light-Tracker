
import json
import math
import sqlite3
from pathlib import Path
from contextlib import closing

import paho.mqtt.client as mqtt


# ============================================================
# 1. PATH DATABASE
# Struktur project:
# HELIOS REVISI/
# ├── server/
# │   ├── create_db.py
# │   ├── database.py
# │   └── mqtt_subscriber.py
# ├── dashboard/
# │   └── app.py
# └── database/
#     └── heliotrops.db
# ============================================================

BASE_DIR = Path(__file__).resolve().parent.parent
DB_DIR = BASE_DIR / "database"
DB_PATH = DB_DIR / "heliotrops.db"

DB_DIR.mkdir(parents=True, exist_ok=True)


# ============================================================
# 2. KONFIGURASI MQTT
# ============================================================

MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
MQTT_KEEPALIVE = 60

MQTT_SENSOR_TOPIC = "heliotrops/sensor_data"


# ============================================================
# 3. DATABASE INITIALIZATION
# ============================================================

def init_database():
    """Membuat tabel sensor dan deduplikasi packet."""

    try:
        with closing(
            sqlite3.connect(DB_PATH, timeout=10)
        ) as conn:

            conn.execute("PRAGMA busy_timeout = 10000")
            conn.execute("PRAGMA journal_mode=WAL")

            conn.execute("""
                CREATE TABLE IF NOT EXISTS sensor_data (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                    ldr_top INTEGER,
                    ldr_bottom INTEGER,
                    ldr_left INTEGER,
                    ldr_right INTEGER,
                    azimuth REAL,
                    elevation REAL,
                    voltage REAL,
                    current REAL,
                    power REAL,
                    energy REAL,
                    tracking_status TEXT
                )
            """)

            # Tabel deduplikasi.
            # Bukan tabel antrean offline.
            conn.execute("""
                CREATE TABLE IF NOT EXISTS received_packets (
                    packet_id TEXT PRIMARY KEY,
                    received_at DATETIME DEFAULT CURRENT_TIMESTAMP
                )
            """)

            conn.commit()

        print(f"[DATABASE] Siap digunakan: {DB_PATH}")
        print("[DATABASE] Tabel sensor_data tersedia.")
        print("[DATABASE] Tabel received_packets tersedia.")

        return True

    except sqlite3.Error as error:

        print(f"[ERROR DATABASE INIT] {error}")

        return False


# ============================================================
# 4. VALIDASI DATA NUMERIK
# ============================================================

def numeric_value(data, key, default=0.0):
    """Mengambil nilai numerik yang aman dari JSON."""

    value = data.get(key, default)

    if value is None or isinstance(value, bool):
        return default

    try:
        number = float(value)

    except (TypeError, ValueError):
        return default

    if not math.isfinite(number):
        return default

    return number


def integer_value(data, key, default=0):
    return int(numeric_value(data, key, default))


# ============================================================
# 5. SIMPAN DATA SENSOR KE SQLITE
# ============================================================

def save_to_db(data):
    """
    Return:
      saved     = packet baru berhasil disimpan
      duplicate = packet sudah pernah disimpan
      failed    = penyimpanan gagal
    """

    packet_id = str(
        data.get("packet_id", "")
    ).strip()

    if not packet_id:

        print("[REJECT] Payload tidak memiliki packet_id.")

        return "failed"

    # Timestamp pengukuran dari ESP32.
    measurement_timestamp = str(
        data.get("timestamp", "TIME_UNSYNCED")
    ).strip()

    if not measurement_timestamp:
        measurement_timestamp = "TIME_UNSYNCED"

    # Validasi status tracking.
    tracking_status = str(
        data.get("tracking_status", "OFF")
    ).upper()

    if tracking_status not in ("ON", "OFF"):
        tracking_status = "OFF"

    # Menyiapkan seluruh nilai sensor.
    values = (
        measurement_timestamp,

        integer_value(data, "ldr_top"),
        integer_value(data, "ldr_bottom"),
        integer_value(data, "ldr_left"),
        integer_value(data, "ldr_right"),

        numeric_value(data, "azimuth"),
        numeric_value(data, "elevation"),

        numeric_value(data, "voltage"),  # Volt
        numeric_value(data, "current"),  # mA
        numeric_value(data, "power"),    # mW
        numeric_value(data, "energy"),   # Wh

        tracking_status,
    )

    try:

        with closing(
            sqlite3.connect(DB_PATH, timeout=10)
        ) as conn:

            conn.execute("PRAGMA busy_timeout = 10000")

            with conn:

                cursor = conn.cursor()

                # Memeriksa apakah packet sudah pernah diterima.
                cursor.execute(
                    """
                    INSERT OR IGNORE INTO received_packets
                    (packet_id)
                    VALUES (?)
                    """,
                    (packet_id,)
                )

                # Packet sudah pernah disimpan.
                if cursor.rowcount == 0:

                    print(
                        f"[DUPLICATE] Packet {packet_id} sudah tersimpan."
                    )

                    return "duplicate"

                # Menyimpan data sensor.
                cursor.execute("""
                    INSERT INTO sensor_data (
                        timestamp,
                        ldr_top,
                        ldr_bottom,
                        ldr_left,
                        ldr_right,
                        azimuth,
                        elevation,
                        voltage,
                        current,
                        power,
                        energy,
                        tracking_status
                    )
                    VALUES (
                        ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?
                    )
                """, values)

        # Transaksi berhasil dan koneksi sudah ditutup.
        print("\n========== DATA HELIOTROPS TERSIMPAN ==========")

        print(f"Packet ID : {packet_id}")
        print(f"Timestamp : {measurement_timestamp}")

        print(f"LDR Top   : {values[1]}")
        print(f"LDR Bottom: {values[2]}")
        print(f"LDR Left  : {values[3]}")
        print(f"LDR Right : {values[4]}")

        print(f"Azimuth   : {values[5]:.2f}")
        print(f"Elevation : {values[6]:.2f}")

        print(f"Voltage   : {values[7]:.3f} V")
        print(f"Current   : {values[8]:.3f} mA")
        print(f"Power     : {values[9]:.3f} mW")
        print(f"Energy    : {values[10]:.6f} Wh")

        print(f"Tracking  : {tracking_status}")

        print("===============================================")

        return "saved"

    except (sqlite3.Error, TypeError, ValueError) as error:

        print(
            f"[ERROR DB] Packet {packet_id} gagal disimpan: {error}"
        )

        return "failed"


# ============================================================
# 6. MQTT CALLBACK: KETIKA BERHASIL TERHUBUNG
# ============================================================

def on_connect(
    client,
    userdata,
    connect_flags,
    reason_code,
    properties
):

    if reason_code == 0:

        print("[MQTT] Berhasil terhubung ke Mosquitto Broker.")

        result, mid = client.subscribe(
            MQTT_SENSOR_TOPIC,
            qos=1
        )

        if result == mqtt.MQTT_ERR_SUCCESS:

            print(
                f"[MQTT] Subscribe: {MQTT_SENSOR_TOPIC} (QoS 1)"
            )

        else:

            print(
                f"[MQTT] Gagal subscribe. Kode: {result}"
            )

    else:

        print(
            f"[MQTT] Koneksi gagal. Reason code: {reason_code}"
        )


# ============================================================
# 7. MQTT CALLBACK: KETIKA TERPUTUS
# ============================================================

def on_disconnect(
    client,
    userdata,
    disconnect_flags,
    reason_code,
    properties
):

    print(
        f"[MQTT] Koneksi terputus. Reason code: {reason_code}"
    )


# ============================================================
# 8. MQTT CALLBACK: MENERIMA DATA ESP32
# ============================================================

def on_message(client, userdata, msg):

    try:

        # Decode payload MQTT.
        payload = msg.payload.decode("utf-8")

        # Parse JSON.
        data = json.loads(payload)

        if not isinstance(data, dict):

            raise ValueError(
                "Payload JSON harus berupa object."
            )

        packet_id = str(
            data.get("packet_id", "")
        ).strip()

        if not packet_id:

            print(
                "[REJECT] Data sensor tidak memiliki packet_id."
            )

            return

        print(
            f"\n[MQTT] Data diterima dari {msg.topic}"
        )

        # Simpan langsung ke SQLite.
        result = save_to_db(data)

        if result == "saved":

            print(
                "[DATABASE] Data berhasil disimpan."
            )

        elif result == "duplicate":

            print(
                "[DATABASE] Packet duplikat dilewati."
            )

        else:

            print(
                "[DATABASE] Data gagal disimpan."
            )

    except UnicodeDecodeError as error:

        print(
            f"[ERROR DECODE] Payload bukan UTF-8: {error}"
        )

    except json.JSONDecodeError as error:

        print(
            f"[ERROR JSON] JSON tidak valid: {error}"
        )

    except Exception as error:

        print(
            f"[ERROR PESAN] {error}"
        )


# ============================================================
# 9. MAIN PROGRAM
# ============================================================

def main():

    # Inisialisasi database.
    if not init_database():

        print(
            "[FATAL] Database tidak siap. Subscriber dihentikan."
        )

        return

    # Membuat MQTT client Paho MQTT 2.x.
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id="heliotrops_python_receiver",
        protocol=mqtt.MQTTv311,
    )

    # Registrasi callback.
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    # Pengaturan reconnect.
    client.reconnect_delay_set(
        min_delay=1,
        max_delay=30
    )

    print("[MQTT] Menghubungkan ke Mosquitto Broker...")

    print(
        f"[MQTT] Broker : {MQTT_BROKER}:{MQTT_PORT}"
    )

    print(
        f"[MQTT] Sensor : {MQTT_SENSOR_TOPIC}"
    )

    print(
        "[MQTT] Mode   : DIRECT DATA RECEIVING"
    )

    print(
        "[MQTT] Offline queue dan application ACK: DISABLED"
    )

    try:

        client.connect(
            MQTT_BROKER,
            MQTT_PORT,
            MQTT_KEEPALIVE
        )

        print(
            "[MQTT] HELIOTROPS Subscriber berjalan. "
            "Tekan CTRL+C untuk berhenti."
        )

        client.loop_forever()

    except KeyboardInterrupt:

        print(
            "\n[MQTT] Program dihentikan oleh pengguna."
        )

    except Exception as error:

        print(
            f"[MQTT ERROR] {error}"
        )

    finally:

        if client.is_connected():
            client.disconnect()

        client.loop_stop()

        print(
            "[MQTT] Subscriber berhenti."
        )


# ============================================================
# 10. PROGRAM ENTRY POINT
# ============================================================

if __name__ == "__main__":
    main()
