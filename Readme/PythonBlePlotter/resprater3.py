import asyncio
import csv
import struct
import time
from datetime import datetime
from collections import deque

import numpy as np
import scipy.signal as signal
from scipy.fft import rfft, rfftfreq

import tkinter as tk
from tkinter import ttk, messagebox

import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from bleak import BleakScanner, BleakClient

CHARACTERISTIC_UUID = "0001e8f2-537e-4f6c-6c04-d104768a1214"
DEVICE_NAME = "ESP32C3_MPU6050"

# Visual Plot Config
MAX_PLOT_POINTS = 200  # Number of raw signal points visible on screen
FS = 100               # Sampling frequency (100 Hz)

# Algorithm Parameters (Matching Updated Notebook)
SG_WINDOW = 31         # Savitzky-Golay window size
SG_POLY = 3            # Savitzky-Golay polynomial order
HP_CUTOFF = 0.5        # High-pass filter cutoff (0.5 Hz)
WINDOW_SIZE = 500      # 500 samples = 5.0s window for real-time calculation

# Band limits for FFT peak search
MIN_BAND_HZ = 0.5      # 0.5 Hz
MAX_BAND_HZ = 4.0      # 4.0 Hz

# Pre-calculate High-Pass Butterworth Filter Coefficients
B_HP, A_HP = signal.butter(N=2, Wn=HP_CUTOFF, btype='highpass', fs=FS)


class macOSBLECollector:
    def __init__(self, root, loop):
        self.root = root
        self.loop = loop
        self.root.title("ESP32-C3 MPU6050 Real-Time Respiration Streamer (Scaled /4 Engine)")
        self.root.geometry("850x900")

        self.client = None
        self.is_streaming = False
        self.is_recording = False
        self.csv_file_path = ""
        self.data_records = []
        self.sample_index = 0

        # Respiration Outputs
        self.adj_fft_bpm = 0.0
        self.adj_peak_bpm = 0.0
        self.raw_freq_hz = 0.0

        # Ring buffers for AccX and AccZ live plots
        self.x_buf = deque(maxlen=MAX_PLOT_POINTS)
        self.acc_x_buf = deque(maxlen=MAX_PLOT_POINTS)
        self.acc_z_buf = deque(maxlen=MAX_PLOT_POINTS)
        
        # Buffer specifically for 500-sample rolling algorithm on AccX
        self.algo_acc_x_buf = deque(maxlen=WINDOW_SIZE)
        
        # Real-time BPM plot buffers
        self.bpm_x_buf = deque(maxlen=MAX_PLOT_POINTS)
        self.bpm_fft_y_buf = deque(maxlen=MAX_PLOT_POINTS)
        self.bpm_peak_y_buf = deque(maxlen=MAX_PLOT_POINTS)

        # Real-time FFT spectrum arrays
        self.plot_freqs = np.array([])
        self.plot_mags = np.array([])
        self.peak_freq = 0.0
        self.peak_mag = 0.0

        self.setup_ui()
        self.setup_plot()

    def setup_ui(self):
        top_frame = ttk.Frame(self.root, padding=10)
        top_frame.pack(side=tk.TOP, fill=tk.X)

        self.lbl_title = ttk.Label(
            top_frame, 
            text="ESP32-C3 BLE Real-Time Respiration Streamer (AccX & AccZ)", 
            font=("Helvetica", 12, "bold")
        )
        self.lbl_title.pack(pady=2)

        self.lbl_status = ttk.Label(top_frame, text="Status: Disconnected", font=("Helvetica", 10))
        self.lbl_status.pack(pady=2)

        btn_frame = ttk.Frame(top_frame)
        btn_frame.pack(pady=5)

        self.btn_connect = ttk.Button(btn_frame, text="Connect BLE Device", command=lambda: self.async_task(self.connect_ble))
        self.btn_connect.grid(row=0, column=0, padx=5)

        self.btn_stream = ttk.Button(btn_frame, text="Start Live Stream", state=tk.DISABLED, command=lambda: self.async_task(self.toggle_stream))
        self.btn_stream.grid(row=0, column=1, padx=5)

        self.btn_record = ttk.Button(btn_frame, text="Start Recording", state=tk.DISABLED, command=self.toggle_recording)
        self.btn_record.grid(row=0, column=2, padx=5)

        self.lbl_values = ttk.Label(top_frame, text="AccX: 0.00 | AccZ: 0.00", font=("Menlo", 10))
        self.lbl_values.pack(pady=3)

        self.lbl_bpm = ttk.Label(
            top_frame, 
            text="Scaled Resp Rate — FFT: -- BPM | Peak: -- BPM", 
            font=("Helvetica", 11, "bold"), 
            foreground="#008080"
        )
        self.lbl_bpm.pack(pady=3)

    def setup_plot(self):
        """Initializes 4 subplots: AccX, AccZ, Scaled BPM Trends, and High-Pass FFT Spectrum."""
        self.fig, (self.ax_x, self.ax_z, self.ax_bpm, self.ax_fft) = plt.subplots(
            4, 1, figsize=(8, 8.0), sharex=False, dpi=100
        )
        self.fig.patch.set_facecolor('#f0f0f0')

        # Line plot primitives
        self.line_x, = self.ax_x.plot([], [], label='AccX (Target Axis)', color='#EF553B', lw=1.2)
        self.line_z, = self.ax_z.plot([], [], label='AccZ', color='#636EFA', lw=1.2)
        
        # BPM Subplot primitives (FFT-based vs Peak-based)
        self.line_bpm_fft, = self.ax_bpm.plot([], [], label='Scaled FFT RR (Raw/4)', color='#00CC96', lw=1.8, marker='o', ms=2)
        self.line_bpm_peak, = self.ax_bpm.plot([], [], label='Scaled Peak RR (Raw/4)', color='#FFA15A', lw=1.5, ls='--')
        
        # Spectrum primitives
        self.line_fft, = self.ax_fft.plot([], [], label='FFT Spectrum (HP Cleaned 0.5Hz)', color='#AB63FA', lw=1.5)
        self.peak_marker, = self.ax_fft.plot([], [], marker='X', color='#EF553B', ms=7, ls='', label='Raw Dominant Peak')

        # Subplot styling
        self.ax_x.set_title("Real-Time Signals & High-Pass Cleaned Respiration Analysis")
        for ax, name in zip([self.ax_x, self.ax_z], ['AccX', 'AccZ']):
            ax.set_ylabel(f"{name} (m/s²)")
            ax.grid(True, linestyle='--', alpha=0.5)
            ax.legend(loc="upper right", fontsize=8)

        self.ax_bpm.set_ylabel("BPM")
        self.ax_bpm.grid(True, linestyle='--', alpha=0.5)
        self.ax_bpm.legend(loc="upper right", fontsize=8)

        self.ax_fft.set_ylabel("Magnitude")
        self.ax_fft.set_xlabel("Frequency (Hz)")
        self.ax_fft.set_xlim(0.4, 4.2)
        self.ax_fft.grid(True, linestyle='--', alpha=0.5)
        self.ax_fft.legend(loc="upper right", fontsize=8)

        self.fig.tight_layout()

        self.canvas = FigureCanvasTkAgg(self.fig, master=self.root)
        self.canvas.get_tk_widget().pack(side=tk.BOTTOM, fill=tk.BOTH, expand=True, padx=10, pady=5)

    def async_task(self, func):
        self.loop.create_task(func())

    async def connect_ble(self):
        self.lbl_status.config(text="Status: Scanning for ESP32C3_MPU6050...")
        self.btn_connect.config(state=tk.DISABLED)

        device = await BleakScanner.find_device_by_name(DEVICE_NAME)
        if not device:
            messagebox.showerror("BLE Error", f"Device '{DEVICE_NAME}' not found.\nEnsure board is powered on.")
            self.lbl_status.config(text="Status: Disconnected")
            self.btn_connect.config(state=tk.NORMAL)
            return

        self.lbl_status.config(text="Status: Connecting...")
        self.client = BleakClient(device)
        try:
            await self.client.connect()
            self.lbl_status.config(text=f"Status: Connected to {DEVICE_NAME}")
            self.btn_stream.config(state=tk.NORMAL)
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))
            self.lbl_status.config(text="Status: Connection Failed")
            self.btn_connect.config(state=tk.NORMAL)

    async def toggle_stream(self):
        if not self.is_streaming:
            await self.client.start_notify(CHARACTERISTIC_UUID, self.notification_handler)
            self.is_streaming = True
            self.btn_stream.config(text="Stop Live Stream")
            self.btn_record.config(state=tk.NORMAL)
            self.lbl_status.config(text="Status: Streaming Data...")
            self.loop.create_task(self.update_plot_loop())
            self.loop.create_task(self.compute_respiration_loop())
        else:
            self.is_streaming = False
            if self.is_recording:
                self.toggle_recording()
            await self.client.stop_notify(CHARACTERISTIC_UUID)
            self.btn_stream.config(text="Start Live Stream")
            self.btn_record.config(state=tk.DISABLED)
            self.lbl_status.config(text="Status: Streaming Stopped")

    def toggle_recording(self):
        if not self.is_recording:
            timestamp_str = datetime.now().strftime("%Y-%m-%dT%H-%M-%S")
            self.csv_file_path = f"imu_raw_{timestamp_str}.csv"
            self.data_records = []
            self.is_recording = True
            self.btn_record.config(text="Stop & Save CSV")
            self.lbl_status.config(text="Status: Streaming & Recording...")
        else:
            self.is_recording = False
            self.save_csv_file()
            self.btn_record.config(text="Start Recording")
            self.lbl_status.config(text="Status: Saved CSV! Streaming...")
            messagebox.showinfo("Saved", f"Data saved successfully to:\n{self.csv_file_path}")

    def notification_handler(self, sender, data):
        if len(data) != 12:
            return

        acc_x, acc_y, acc_z = struct.unpack("<fff", data)

        # Buffer incoming values (AccX and AccZ)
        self.x_buf.append(self.sample_index)
        self.acc_x_buf.append(acc_x)
        self.acc_z_buf.append(acc_z)
        
        # Dedicated buffer for 500-sample sliding window algorithm
        self.algo_acc_x_buf.append(acc_x)

        if self.is_recording:
            self.data_records.append({
                "Index": self.sample_index,
                "AccX": acc_x,
                "AccY": acc_y,
                "AccZ": acc_z,
                "GyroX": 0.0,
                "GyroY": 0.0,
                "GyroZ": 0.0
            })

        self.sample_index += 1
        self.lbl_values.config(text=f"AccX: {acc_x:6.2f} | AccZ: {acc_z:6.2f}")

    async def compute_respiration_loop(self):
        """Asynchronously executes SG, Butterworth High-Pass, FFT, and Peak Respiration with /4 Scaling."""
        while self.is_streaming:
            if len(self.algo_acc_x_buf) == WINDOW_SIZE:
                raw_signal = np.array(self.algo_acc_x_buf)
                N = len(raw_signal)

                # 1. Savitzky-Golay Denoising
                sg_signal = signal.savgol_filter(raw_signal, window_length=SG_WINDOW, polyorder=SG_POLY)

                # 2. High-Pass Filter (0.5 Hz Cutoff)
                cleaned_signal = signal.filtfilt(B_HP, A_HP, sg_signal)

                # -------------------------------------------------------------
                # Method A: High-Pass FFT Peak Extraction
                # -------------------------------------------------------------
                fft_vals = np.fft.rfft(cleaned_signal)
                fft_freqs = np.fft.rfftfreq(N, d=1.0 / FS)
                magnitude = np.abs(fft_vals) / N
                magnitude[1:] = 2 * magnitude[1:]

                # Isolate target frequency window (0.5 Hz to 4.0 Hz)
                spectrum_mask = (fft_freqs >= MIN_BAND_HZ) & (fft_freqs <= MAX_BAND_HZ)
                self.plot_freqs = fft_freqs[spectrum_mask]
                self.plot_mags = magnitude[spectrum_mask]

                if np.any(spectrum_mask):
                    peak_idx_local = np.argmax(magnitude[spectrum_mask])
                    self.peak_freq = self.plot_freqs[peak_idx_local]
                    self.peak_mag = self.plot_mags[peak_idx_local]
                    raw_fft_bpm = self.peak_freq * 60.0
                else:
                    self.peak_freq, self.peak_mag, raw_fft_bpm = 0.0, 0.0, 0.0

                # -------------------------------------------------------------
                # Method B: Peak-to-Peak Time-Domain Calculation
                # -------------------------------------------------------------
                min_gap = int(FS / 4.0)  # 0.25s gap between sub-peaks
                peaks_idx, _ = signal.find_peaks(
                    cleaned_signal, 
                    distance=min_gap, 
                    prominence=np.std(cleaned_signal) * 0.3
                )

                if len(peaks_idx) > 1:
                    time_diffs = np.diff(peaks_idx) / FS
                    raw_peak_bpm = 60.0 / np.mean(time_diffs)
                else:
                    raw_peak_bpm = 0.0

                # -------------------------------------------------------------
                # Apply Division by 4 Scaling
                # -------------------------------------------------------------
                self.adj_fft_bpm = round(raw_fft_bpm / 4.0, 1)
                self.adj_peak_bpm = round(raw_peak_bpm / 4.0, 1)

                # Update plot trend buffers
                self.bpm_x_buf.append(self.sample_index)
                self.bpm_fft_y_buf.append(self.adj_fft_bpm)
                self.bpm_peak_y_buf.append(self.adj_peak_bpm)

                # Update UI Status
                self.lbl_bpm.config(
                    text=f"Scaled Resp Rate — FFT: {self.adj_fft_bpm:.1f} BPM | Peak: {self.adj_peak_bpm:.1f} BPM (Raw {self.peak_freq:.2f}Hz)"
                )

            await asyncio.sleep(0.5)  # Runs every 0.5s

    async def update_plot_loop(self):
        """Asynchronously updates canvas across all 4 subplots."""
        while self.is_streaming:
            if self.x_buf:
                # 1. Update AccX & AccZ signals
                self.line_x.set_data(self.x_buf, self.acc_x_buf)
                self.line_z.set_data(self.x_buf, self.acc_z_buf)

                x_min, x_max = self.x_buf[0], self.x_buf[-1] + 1

                for ax, buf in zip([self.ax_x, self.ax_z], [self.acc_x_buf, self.acc_z_buf]):
                    ax.set_xlim(x_min, x_max)
                    if buf:
                        min_y, max_y = min(buf), max(buf)
                        margin = max(0.5, (max_y - min_y) * 0.1)
                        ax.set_ylim(min_y - margin, max_y + margin)

                # 2. Update Scaled Respiration Curves
                if self.bpm_x_buf:
                    self.line_bpm_fft.set_data(self.bpm_x_buf, self.bpm_fft_y_buf)
                    self.line_bpm_peak.set_data(self.bpm_x_buf, self.bpm_peak_y_buf)
                    
                    bpm_x_min, bpm_x_max = self.bpm_x_buf[0], self.bpm_x_buf[-1] + 1
                    self.ax_bpm.set_xlim(bpm_x_min, bpm_x_max)
                    
                    combined_bpm = list(self.bpm_fft_y_buf) + list(self.bpm_peak_y_buf)
                    if combined_bpm:
                        min_bpm = max(0, min(combined_bpm) - 2)
                        max_bpm = max(combined_bpm) + 2
                        self.ax_bpm.set_ylim(min_bpm, max_bpm)

                # 3. Update Real-Time High-Pass Spectrum
                if len(self.plot_freqs) > 0 and len(self.plot_mags) > 0:
                    self.line_fft.set_data(self.plot_freqs, self.plot_mags)
                    self.peak_marker.set_data([self.peak_freq], [self.peak_mag])

                    max_mag = np.max(self.plot_mags)
                    self.ax_fft.set_ylim(0, max_mag * 1.15)

                self.canvas.draw_idle()

            await asyncio.sleep(0.05)

    def save_csv_file(self):
        with open(self.csv_file_path, "w", newline="") as f:
            f.write("Device,ESP32C3_MPU6050\n")
            f.write(f"Timestamp,{datetime.now().isoformat()}\n")
            f.write(f"SampleCount,{len(self.data_records)}\n")
            f.write("SamplingRate,100Hz\n\n")

            writer = csv.DictWriter(f, fieldnames=["Index", "AccX", "AccY", "AccZ", "GyroX", "GyroY", "GyroZ"])
            writer.writeheader()
            writer.writerows(self.data_records)

    async def run(self):
        while True:
            self.root.update()
            await asyncio.sleep(0.01)


if __name__ == "__main__":
    root = tk.Tk()
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    app = macOSBLECollector(root, loop)

    try:
        loop.run_until_complete(app.run())
    except tk.TclError:
        pass