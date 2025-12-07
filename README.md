# 📁 File Tracker

Aplikasi Python untuk melacak dan memonitor file yang dibuka di sistem Windows.

## ✨ Fitur

- **Scan File Terbuka**: Melihat semua file yang sedang dibuka oleh proses di sistem
- **Filter by Process**: Filter berdasarkan nama aplikasi (e.g., notepad, chrome)
- **Filter by Extension**: Filter berdasarkan ekstensi file (.txt, .py, .docx, dll)
- **Real-time Monitoring**: Monitor aktivitas file secara real-time di direktori tertentu
- **Activity Logging**: Catat semua aktivitas file ke log
- **Export Data**: Export ke format JSON atau CSV

## 🚀 Instalasi

1. Clone atau download repository ini
2. Install dependencies:

```bash
pip install -r requirements.txt
```

## 📖 Cara Penggunaan

### 1. Scan File yang Sedang Dibuka

```bash
# Scan semua file yang sedang dibuka
python file_tracker.py scan

# Scan file yang dibuka oleh aplikasi tertentu
python file_tracker.py scan -p notepad
python file_tracker.py scan -p chrome

# Scan file dengan ekstensi tertentu
python file_tracker.py scan -e .txt .py .docx

# Kombinasi filter
python file_tracker.py scan -p vscode -e .py .js

# Export hasil ke JSON
python file_tracker.py scan -o hasil_scan.json
```

### 2. Monitor Aktivitas File Real-time

```bash
# Monitor direktori tertentu
python file_tracker.py monitor -d C:\Users\Documents

# Monitor direktori saat ini
python file_tracker.py monitor -d .

# Monitor dengan filter ekstensi
python file_tracker.py monitor -d . -e .py .txt

# Monitor multiple direktori
python file_tracker.py monitor -d C:\Projects D:\Data
```

### 3. Lihat Riwayat Aktivitas

```bash
# Tampilkan 20 aktivitas terbaru
python file_tracker.py history

# Tampilkan N aktivitas terbaru
python file_tracker.py history -n 50

# Export riwayat ke JSON
python file_tracker.py history --export-json riwayat.json

# Export riwayat ke CSV
python file_tracker.py history --export-csv riwayat.csv

# Hapus semua riwayat
python file_tracker.py history --clear
```

## 📊 Contoh Output

### Scan Output
```
================================================================================
📊 FILE YANG SEDANG DIBUKA
================================================================================

🔹 Process: Code.exe (15 file)
------------------------------------------------------------
     1. C:\Projects\myapp\main.py
     2. C:\Projects\myapp\utils.py
     3. C:\Projects\myapp\config.json

🔹 Process: chrome.exe (8 file)
------------------------------------------------------------
     1. C:\Users\AppData\Local\Google\Chrome\...
```

### Monitor Output
```
2024-12-04 10:30:15 | INFO     | [CREATED] C:\Projects\test.py
2024-12-04 10:30:20 | INFO     | [MODIFIED] C:\Projects\main.py
2024-12-04 10:30:25 | INFO     | [DELETED] C:\Projects\temp.txt
```

## 🔧 Penggunaan sebagai Library

```python
from file_tracker import FileTracker

# Inisialisasi tracker
tracker = FileTracker(
    extensions=['.py', '.txt', '.docx'],
    log_file='my_tracker.log'
)

# Scan file yang sedang dibuka
open_files = tracker.get_open_files_by_process('notepad')
for file_info in open_files:
    print(f"{file_info.process_name}: {file_info.file_path}")

# Monitor direktori
tracker.start_monitoring(['C:\\Projects'])
```

## 📋 API Reference

### Class: `FileTracker`

#### Constructor Parameters:
- `watch_directories`: List direktori untuk dimonitor
- `extensions`: List ekstensi file untuk difilter
- `log_file`: Path file log (default: "file_tracker.log")
- `activity_file`: Path file aktivitas JSON (default: "file_activities.json")

#### Methods:
- `get_open_files_by_process(process_name)` - Dapatkan file yang dibuka oleh proses
- `scan_open_files(process_name)` - Scan dan kategorikan file berdasarkan proses
- `print_open_files(process_name)` - Print file yang dibuka dalam format readable
- `start_monitoring(directories)` - Mulai monitoring real-time
- `stop_monitoring()` - Hentikan monitoring
- `get_recent_activities(limit)` - Dapatkan aktivitas terbaru
- `export_to_json(output_file)` - Export ke JSON
- `export_to_csv(output_file)` - Export ke CSV

## ⚠️ Catatan Penting

1. **Hak Akses Admin**: Beberapa proses sistem mungkin memerlukan hak akses administrator untuk di-scan
2. **Performa**: Scanning semua proses bisa memakan waktu jika banyak aplikasi yang berjalan
3. **Windows Only**: Tool ini dioptimalkan untuk Windows, beberapa fitur mungkin berbeda di OS lain

## 📄 License

MIT License - Bebas digunakan dan dimodifikasi.

