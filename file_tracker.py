"""
File Tracker - Melacak file yang dibuka oleh user (double click)
Author: AI Agent
Version: 2.1.0

Fitur:
- Melacak file yang dibuka user melalui double click
- Memonitor folder Recent Windows
- Format JSON dengan previous_session dan current_session
"""

import os
import sys
import time
import json
import subprocess
from datetime import datetime
from pathlib import Path
from typing import Dict, Optional


# ============================================================================
# Main File Tracker Class
# ============================================================================

class FileTracker:
    def __init__(self, activity_file: str = "file_activity.json"):
        self.activity_file = activity_file
        
        self.data: Dict = {
            "previous_session": {},
            "current_session": {}
        }
        
        self.recent_folder = Path(os.environ['APPDATA']) / 'Microsoft' / 'Windows' / 'Recent'
        
        self.known_shortcuts: Dict[str, float] = {}  
        
        self._load_and_rotate_session()
        
        self._initial_scan()
    
    def _is_valid_file(self, target_path: str) -> bool:
        return os.path.isfile(target_path)
    
    def _load_and_rotate_session(self):
        """Load data dan rotasi session (current -> previous)"""
        if os.path.exists(self.activity_file):
            try:
                with open(self.activity_file, 'r', encoding='utf-8') as f:
                    existing_data = json.load(f)
                
                # Gabungkan previous_session yang lama dengan current_session yang lama
                old_previous = existing_data.get("previous_session", {})
                old_current = existing_data.get("current_session", {})
                
                # Gabungkan: previous + current menjadi previous baru
                self.data["previous_session"] = {**old_previous, **old_current}
                self.data["current_session"] = {}
                
                total_previous = len(self.data["previous_session"])
                print(f"📂 Loaded {total_previous} file dari session sebelumnya")
                
                # Simpan rotasi
                self._save_data()
                
            except (json.JSONDecodeError, TypeError) as e:
                print(f"⚠️ Gagal load data: {e}")
                self.data = {"previous_session": {}, "current_session": {}}
        else:
            print("📂 Memulai tracking baru...")
    
    def _save_data(self):
        """Simpan data ke file JSON"""
        try:
            with open(self.activity_file, 'w', encoding='utf-8') as f:
                json.dump(self.data, f, ensure_ascii=False, indent=4)
        except IOError as e:
            print(f"❌ Gagal menyimpan: {e}")
    
    def _initial_scan(self):
        """Scan awal untuk mendapatkan baseline shortcuts yang sudah ada"""
        print("🔍 Scanning folder Recent...")
        
        for shortcut_file in self.recent_folder.glob("*.lnk"):
            try:
                mtime = shortcut_file.stat().st_mtime
                self.known_shortcuts[str(shortcut_file)] = mtime
            except (OSError, FileNotFoundError):
                continue
        
        print(f"📋 Baseline: {len(self.known_shortcuts)} file di Recent folder")
    
    def _resolve_shortcut(self, shortcut_path: str) -> Optional[str]:
        """Resolve .lnk shortcut ke path asli file menggunakan PowerShell"""
        try:
            ps_command = f'''
            $shell = New-Object -ComObject WScript.Shell
            $shortcut = $shell.CreateShortcut("{shortcut_path}")
            $shortcut.TargetPath
            '''
            
            result = subprocess.run(
                ['powershell', '-Command', ps_command],
                capture_output=True,
                text=True,
                creationflags=subprocess.CREATE_NO_WINDOW
            )
            
            target_path = result.stdout.strip()
            print(result.stdout)
            return target_path if target_path else None
            
        except Exception:
            return None
    
    def _check_for_new_files(self) -> list:
        """Cek apakah ada file baru yang dibuka"""
        new_files = []
        
        for shortcut_file in self.recent_folder.glob("*.lnk"):
            shortcut_path = str(shortcut_file)
            
            try:
                mtime = shortcut_file.stat().st_mtime
            except (OSError, FileNotFoundError):
                continue
            
            # Cek apakah ini shortcut baru atau dimodifikasi
            if shortcut_path not in self.known_shortcuts or self.known_shortcuts[shortcut_path] < mtime:
                # Resolve shortcut ke file asli
                target_path = self._resolve_shortcut(shortcut_path)
                
                if target_path and os.path.exists(target_path):
                    # Skip jika bukan file (folder akan di-skip)
                    if not self._is_valid_file(target_path):
                        self.known_shortcuts[shortcut_path] = mtime
                        continue
                    
                    file_name = Path(target_path).name
                    timestamp = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
                    new_files.append((file_name, timestamp))
                
                # Update known shortcuts
                self.known_shortcuts[shortcut_path] = mtime
        
        return new_files
    
    def record_file(self, file_name: str, timestamp: str):
        """Catat file baru ke current_session"""
        self.data["current_session"][file_name] = timestamp
        self._save_data()
        
        # Print notification
        print(f"  📄 {file_name}")
    
    def start_tracking(self, interval: int = 2):
        """
        Mulai tracking file yang dibuka user.
        
        Args:
            interval: Interval pengecekan dalam detik (default: 2)
        """
        print("\n" + "="*60)
        print("🔄 FILE TRACKER AKTIF")
        print("="*60)
        print(f"📌 Memonitor: {self.recent_folder}")
        print(f"📌 Interval: setiap {interval} detik")
        print(f"📌 Output: {self.activity_file}")
        print("📌 Tekan Ctrl+C untuk berhenti")
        print("="*60)
        print("\n⏳ Menunggu file dibuka...\n")
        
        try:
            while True:
                # Cek file baru
                new_files = self._check_for_new_files()
                
                if new_files:
                    current_time = datetime.now().strftime("%H:%M:%S")
                    print(f"[{current_time}] 📂 File dibuka:")
                    
                    for file_name, timestamp in new_files:
                        self.record_file(file_name, timestamp)
                    
                    print()
                
                time.sleep(interval)
                
        except KeyboardInterrupt:
            self._on_stop()
    
    def _on_stop(self):
        """Handler ketika tracking dihentikan"""
        print("\n\n" + "="*60)
        print("🛑 TRACKING DIHENTIKAN")
        print("="*60)
        
        current_count = len(self.data["current_session"])
        previous_count = len(self.data["previous_session"])
        
        print(f"📊 File session ini: {current_count}")
        print(f"📊 File session sebelumnya: {previous_count}")
        print(f"💾 Disimpan ke: {self.activity_file}")
        
        # Tampilkan file terakhir dari current session
        if self.data["current_session"]:
            print("\n📋 File dibuka session ini:")
            items = list(self.data["current_session"].items())[-5:]
            for file_name, timestamp in items:
                time_part = timestamp.split('T')[1] if 'T' in timestamp else timestamp
                print(f"   [{time_part}] {file_name}")
        
        print("\n✅ Selesai!")
    
    def print_history(self, limit: int = 20):
        """Tampilkan riwayat file yang dibuka"""
        print("\n" + "="*60)
        print("📋 RIWAYAT FILE DIBUKA")
        print("="*60)
        
        # Current session
        print(f"\n🟢 CURRENT SESSION ({len(self.data['current_session'])} file):")
        print("-"*40)
        if self.data["current_session"]:
            items = list(self.data["current_session"].items())[-limit:]
            for file_name, timestamp in items:
                print(f"  {timestamp}  {file_name}")
        else:
            print("  (kosong)")
        
        # Previous session
        print(f"\n🔵 PREVIOUS SESSION ({len(self.data['previous_session'])} file):")
        print("-"*40)
        if self.data["previous_session"]:
            items = list(self.data["previous_session"].items())[-limit:]
            for file_name, timestamp in items:
                print(f"  {timestamp}  {file_name}")
        else:
            print("  (kosong)")
        
        print("\n" + "="*60)
    
    def clear_all(self):
        """Hapus semua riwayat"""
        self.data = {"previous_session": {}, "current_session": {}}
        self._save_data()
        print("✅ Semua riwayat telah dihapus!")
    
    def clear_previous(self):
        """Hapus hanya previous session"""
        self.data["previous_session"] = {}
        self._save_data()
        print("✅ Previous session telah dihapus!")


# ============================================================================
# Main Entry Point
# ============================================================================

def main():
    """Main entry point"""
    import argparse
    
    parser = argparse.ArgumentParser(
        description="File Tracker - Melacak file yang dibuka user"
    )
    parser.add_argument('command', nargs='?', default='track',
                        choices=['track', 'history', 'clear', 'clear-prev'],
                        help='track=mulai tracking, history=lihat riwayat, clear=hapus semua, clear-prev=hapus previous')
    parser.add_argument('-n', '--limit', type=int, default=20,
                        help='Jumlah file untuk ditampilkan (untuk history)')
    parser.add_argument('-i', '--interval', type=int, default=2,
                        help='Interval scan dalam detik (default: 2)')
    
    args = parser.parse_args()
    
    print("""
╔═══════════════════════════════════════════════════════════════╗
║                    FILE TRACKER v2.1                          ║
║         Melacak File yang Dibuka oleh User                    ║
╚═══════════════════════════════════════════════════════════════╝
    """)
    
    tracker = FileTracker()
    
    if args.command == 'track':
        tracker.start_tracking(args.interval)
    
    elif args.command == 'history':
        tracker.print_history(args.limit)
    
    elif args.command == 'clear':
        tracker.clear_all()
    
    elif args.command == 'clear-prev':
        tracker.clear_previous()


if __name__ == "__main__":
    main()
