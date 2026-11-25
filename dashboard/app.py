import streamlit as st
import pandas as pd
import plotly.graph_objects as go
from datetime import datetime
import time 
import sqlite3
from pathlib import Path
import paho.mqtt.client as mqtt
import json

# ============================================================
# KONFIGURASI HALAMAN
# ============================================================
st.set_page_config(
    page_title="Smart Solar Light Tracker",
    page_icon="☀️",
    layout="wide",
    initial_sidebar_state="collapsed"
)

# ============================================================
# CSS CUSTOM (design dipertahankan)
# ============================================================
st.markdown("""
<style>
    .block-container { padding-top: 2rem; padding-bottom: 2rem; }
    div[data-testid="stMetric"] {
        background: linear-gradient(135deg, #1e1e2e 0%, #2a2a3e 100%);
        padding: 16px;
        border-radius: 12px;
        border-left: 4px solid #FFA500;
    }
    div[data-testid="stMetricLabel"] { color: #bbb !important; }
    div[data-testid="stMetricValue"] { color: #FFA500 !important; }
    .status-box {
        padding: 12px 16px;
        border-radius: 10px;
        background: #1e1e2e;
        border: 1px solid #333;
        margin-bottom: 8px;
        text-align: center;
    }
    .status-box b { color: #FFA500; }
    h1 { color: #FFA500; }
    /* sembunyikan sidebar total */
    [data-testid="collapsedControl"] { display: none; }
    section[data-testid="stSidebar"] { display: none; }
</style>
""", unsafe_allow_html=True)

# ============================================================
# KONEKSI DATABASE & MQTT PUBLISHER (UNTUK KONTROL KENDALI)
# ============================================================
BASE_DIR = Path(__file__).resolve().parent
# app.py berada di folder dashboard, sedangkan database berada sejajar dengan dashboard.
DB_PATH = BASE_DIR.parent / "database" / "heliotrops.db"

MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
MQTT_CONTROL_TOPIC = "heliotrops/control"

def send_mqtt_control(command):
    client = None

    try:
        # Kompatibilitas Paho MQTT versi 1.x dan 2.x
        if hasattr(mqtt, "CallbackAPIVersion"):
            client = mqtt.Client(
                callback_api_version=mqtt.CallbackAPIVersion.VERSION2
            )
        else:
            client = mqtt.Client()

        # Hubungkan ke broker
        client.connect(MQTT_BROKER, MQTT_PORT, 60)

        # Jalankan network loop
        client.loop_start()

        # Buat payload JSON
        payload = json.dumps({"command": command})

        # Kirim perintah dengan QoS 1
        info = client.publish(
            MQTT_CONTROL_TOPIC,
            payload,
            qos=1
        )

        # Tunggu hingga pesan terkirim
        info.wait_for_publish(timeout=5)

        if not info.is_published():
            raise TimeoutError("Pesan MQTT belum berhasil dikirim")

        print(f"[MQTT] Perintah berhasil dikirim: {command}")

    except Exception as e:
        print(f"[MQTT ERROR] Gagal mengirim perintah: {e}")

    finally:
        if client is not None:
            try:
                client.disconnect()
                client.loop_stop()
            except Exception:
                pass

# ============================================================
# SESSION STATE INISIALISASI
# ============================================================
if "tracking" not in st.session_state:
    st.session_state.tracking = True

# ============================================================
# AMBIL DATA NYATA DARI DATABASE SQLite
# ============================================================
@st.cache_data(ttl=1)
def load_data_from_db():
    if not DB_PATH.exists():
        return pd.DataFrame(columns=[
            "id", "timestamp", "ldr_top", "ldr_bottom", "ldr_left", "ldr_right",
            "azimuth", "elevation", "voltage", "current", "power", "energy",
            "tracking_status", "timestamp"
        ])
    
    try:
        with sqlite3.connect(DB_PATH) as conn:
            query = "SELECT * FROM sensor_data ORDER BY id DESC LIMIT 100"
            df = pd.read_sql(query, conn)
        return df
    except (sqlite3.Error, pd.errors.DatabaseError) as e:
        st.error(f"Gagal membaca database HELIOTROPS: {e}")
        return pd.DataFrame()

df_db = load_data_from_db()

# Tentukan nilai real-time dari data terbaru di database (Baris teratas)
if not df_db.empty:
    latest = df_db.iloc[0]
    
    # Hitung rata-rata / estimasi intensitas cahaya dari 4 LDR
    top_v = latest.get("ldr_top", 0)
    bot_v = latest.get("ldr_bottom", 0)
    left_v = latest.get("ldr_left", 0)
    right_v = latest.get("ldr_right", 0)
    lux = round((top_v + bot_v + left_v + right_v) / 4.0, 1)
    
    az = float(latest.get("azimuth", 0.0) or 0.0)
    el = float(latest.get("elevation", 0.0) or 0.0)
    voltage = float(latest.get("voltage", 0.0) or 0.0)
    current_mA = float(latest.get("current", 0.0) or 0.0)
    
    # Tentukan arah mata angin berdasarkan sudut azimuth
    arah_list = ["Utara", "Timur Laut", "Timur", "Tenggara",
                 "Selatan", "Barat Daya", "Barat", "Barat Laut"]
    arah = arah_list[int(az // 45) % 8]
    
    db_tracking_status = str(latest.get("tracking_status", "ON"))
    st.session_state.tracking = True if db_tracking_status == "ON" else False
else:
    lux, az, el, arah = 0.0, 0.0, 0.0, "Utara"
    voltage, current_mA = 0.0, 0.0

# ============================================================
# HEADER
# ============================================================
st.title("☀️ Smart Solar Light Tracker")
st.caption(f"Update: {datetime.now().strftime('%A, %d %B %Y — %H:%M:%S')}")
st.markdown("---")

# ============================================================
# MONITORING REAL-TIME
# ============================================================
st.subheader("📊 Monitoring Real-Time")

# Hitung selisih delta lux dari data sebelumnya jika tersedia
if len(df_db) > 1:
    prev_top = df_db.iloc[1]["ldr_top"]
    prev_bot = df_db.iloc[1]["ldr_bottom"]
    prev_left = df_db.iloc[1]["ldr_left"]
    prev_right = df_db.iloc[1]["ldr_right"]
    prev_lux = (prev_top + prev_bot + prev_left + prev_right) / 4.0
    delta_lux = round(lux - prev_lux, 1)
else:
    delta_lux = 0.0

# Enam kartu monitoring dibagi menjadi dua baris, masing-masing tiga kolom.
# Desain metric/CSS tetap menggunakan styling dashboard yang sudah ada.
c1, c2, c3 = st.columns(3)
c1.metric("💡 Intensitas Cahaya", f"{lux} ADC", f"{delta_lux}")
c2.metric("🧭 Arah Cahaya", arah)
c3.metric("📐 Azimuth", f"{az:.1f}°")

c4, c5, c6 = st.columns(3)
c4.metric("📐 Elevasi", f"{el:.1f}°")
c5.metric("⚡ Voltage Panel", f"{voltage:.2f} V")
c6.metric("🔌 Current Panel", f"{current_mA:.2f} mA")

# ============================================================
# PANEL TRACKER (KONTROL JARAK JAUH)
# ============================================================
st.markdown("### 🛰️ Kontrol Tracker")

t1, t2, t3 = st.columns([1, 1, 2])

with t1:
    if st.button("▶️ Mulai Tracker", use_container_width=True):
        send_mqtt_control("ON")
        st.session_state.tracking = True
        st.toast("Perintah Kirim: Tracker diaktifkan")
        time.sleep(0.4)
        st.rerun()

with t2:
    if st.button("⏹️ Stop Tracker", use_container_width=True):
        send_mqtt_control("OFF")
        st.session_state.tracking = False
        st.toast("Perintah Kirim: Tracker dihentikan")
        time.sleep(0.4)
        st.rerun()

with t3:
    if st.session_state.tracking:
        st.markdown(f"""<div class="status-box" style="border-left:4px solid #22c55e;">
<b>Status Tracker</b><br>🟢 AKTIF — panel mengikuti cahaya
</div>""", unsafe_allow_html=True)
    else:
        st.markdown(f"""<div class="status-box" style="border-left:4px solid #ef4444;">
<b>Status Tracker</b><br>⏸️ BERHENTI — panel diam di posisi terakhir
</div>""", unsafe_allow_html=True)

st.markdown("---")

# ============================================================
# GRAFIK INTENSITAS CAHAYA (REAL-TIME DARI DATABASE)
# ============================================================
st.subheader("📈 Grafik Intensitas Cahaya")

if not df_db.empty:
    # Siapkan data grafik berurutan dari waktu terlama ke terbaru
    df_chart = df_db.copy()
    df_chart["avg_lux"] = (df_chart["ldr_top"] + df_chart["ldr_bottom"] + df_chart["ldr_left"] + df_chart["ldr_right"]) / 4.0
    df_chart = df_chart.sort_values("timestamp")
    
    fig = go.Figure()
    fig.add_trace(go.Scatter(
        x=df_chart["timestamp"],
        y=df_chart["avg_lux"],
        mode="lines+markers",
        name="Intensitas (lux)",
        line=dict(color="#FFA500", width=3),
        marker=dict(size=6),
        fill="tozeroy",
        fillcolor="rgba(255,165,0,0.15)"
    ))
    fig.update_layout(
        xaxis_title="Waktu",
        yaxis_title="Intensitas Rata-rata (ADC)",
        template="plotly_dark",
        height=380,
        margin=dict(l=20, r=20, t=20, b=20),
        hovermode="x unified"
    )
    st.plotly_chart(fig, use_container_width=True)
else:
    st.info("Belum ada data sensor masuk di database. Pastikan ESP32 dan skrip subscriber MQTT aktif.")

st.markdown("---")

# ============================================================
# RIWAYAT DATABASE (ASLI DARI SQLITE)
# ============================================================
st.subheader("🗄️ Riwayat Database")

if not df_db.empty:
    col_a, col_b = st.columns([1, 3])
    with col_a:
        limit = st.selectbox("Tampilkan baris:", [10, 20, 30, 50], index=2)

    df_show = df_db.head(limit)
    st.dataframe(df_show, use_container_width=True, hide_index=True, height=350)

    st.download_button(
        "⬇️ Download CSV (Database)",
        df_show.to_csv(index=False).encode("utf-8"),
        file_name=f"heliotrops_db_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv",
        mime="text/csv"
    )

    with st.expander("📊 Grafik riwayat dari database"):
        df_rev = df_db.sort_values("timestamp")
        fig2 = go.Figure()
        
        # Hitung rata-rata LDR untuk grafik riwayat
        df_rev["avg_lux"] = (df_rev["ldr_top"] + df_rev["ldr_bottom"] + df_rev["ldr_left"] + df_rev["ldr_right"]) / 4.0
        
        fig2.add_trace(go.Scatter(
            x=df_rev["timestamp"],
            y=df_rev["avg_lux"],
            mode="lines",
            line=dict(color="#00BFFF", width=2),
            name="Intensitas"
        ))
        fig2.update_layout(
            xaxis_title="Waktu",
            yaxis_title="Intensitas (ADC)",
            template="plotly_dark",
            height=350
        )
        st.plotly_chart(fig2, use_container_width=True)
else:
    st.info("Database SQLite masih kosong.")

# ============================================================
# AUTO REFRESH (SINKRONISASI DATA REAL-TIME)
# ============================================================
time.sleep(0.4)
st.rerun()