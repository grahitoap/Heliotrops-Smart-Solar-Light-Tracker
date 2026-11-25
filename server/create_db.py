
import sqlite3
from pathlib import Path

# ==========================================
# DATABASE CONFIGURATION
# ==========================================

# Mengambil folder utama proyek HELIOS REVISI
BASE_DIR = Path(__file__).resolve().parent.parent

# Menentukan lokasi database
DB_DIR = BASE_DIR / "database"
DB_PATH = DB_DIR / "heliotrops.db"

# Membuat folder database jika belum tersedia
DB_DIR.mkdir(parents=True, exist_ok=True)


# ==========================================
# DATABASE INITIALIZATION
# ==========================================

def create_database():

    connection = sqlite3.connect(DB_PATH)

    try:
        cursor = connection.cursor()

        # ----------------------------------
        # TABEL SENSOR DATA
        # ----------------------------------

        cursor.execute("""
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

        # ----------------------------------
        # TABEL RECEIVED PACKETS
        # Untuk mencegah data duplikat MQTT
        # ----------------------------------

        cursor.execute("""
        CREATE TABLE IF NOT EXISTS received_packets (
            packet_id TEXT PRIMARY KEY,
            received_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
        """)

        connection.commit()

        print("DATABASE HELIOTROPS BERHASIL DIINISIALISASI!")
        print(f"Lokasi database: {DB_PATH}")
        print("Tabel sensor_data siap digunakan.")
        print("Tabel received_packets siap digunakan.")

    except sqlite3.Error as error:
        connection.rollback()
        print(f"Database error: {error}")

    finally:
        connection.close()


# ==========================================
# MAIN PROGRAM
# ==========================================

if __name__ == "__main__":
    create_database()
