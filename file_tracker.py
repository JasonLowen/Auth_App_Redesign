"""
File Tracker - Melacak file yang dibuka oleh user (double click)

Fitur:
- Melacak file yang dibuka user melalui double click
- Memonitor folder Recent Windows
- Format JSON dengan previous_session dan current_session
- File rotation: hanya saat stop (Ctrl+C), dengan threshold logic
"""
# ============================================================================
# IMPORTS - What we need to make this program work
# ============================================================================

import os           # For checking if files and folders exist
import sys          # For system-level operations
import time         # For making the program wait/pause
import json         # For saving data in a readable format (like a notebook)
import subprocess   # For running PowerShell commands (talking to Windows)
from datetime import datetime   # For getting current date and time
from pathlib import Path        # For working with file paths (like addresses)
from typing import Dict, Optional  # For making code clearer (just labels)


# ============================================================================
# Main File Tracker Class
# ============================================================================

class FileTracker:
    def __init__(self, activity_file: str = "file_activity.json", rotation_threshold: int = 3):
        # Where we save our tracking data (like a diary)
        self.activity_file = activity_file
        
        # Rotation threshold: when current_session reaches this many files, rotate ON STOP
        self.rotation_threshold = rotation_threshold
        
        # Our data storage: two buckets for tracking files
        # Think of it like: "files opened today" vs "files opened yesterday"
        self.data: Dict = {
            "previous_session": {},  # Old files from last time
            "current_session": {}    # New files we're tracking now
        }
        
        # Windows' special folder where it keeps shortcuts to recently opened files
        # It's like Windows' own diary of what you opened
        self.recent_folder = Path(os.environ['APPDATA']) / 'Microsoft' / 'Windows' / 'Recent'
        
        # Our memory: remember which shortcuts we've already seen
        # Stores: shortcut name → when it was last changed
        self.known_shortcuts: Dict[str, float] = {}  
        
        # Load existing data WITHOUT rotating (just load as-is)
        self._load_data()
        
        # Look at what shortcuts are already there (so we know what's new later)
        self._initial_scan()
    
    def _is_valid_file(self, target_path: str) -> bool:
        """Check: Is this an actual file? (not a folder)"""
        return os.path.isfile(target_path)
    
    def _load_data(self):
        """
        Load existing data as-is (no rotation on startup)
        """
        if os.path.exists(self.activity_file):
            try:
                with open(self.activity_file, 'r', encoding='utf-8') as f:
                    self.data = json.load(f)
                
                # Ensure both keys exist
                if "previous_session" not in self.data:
                    self.data["previous_session"] = {}
                if "current_session" not in self.data:
                    self.data["current_session"] = {}
                
                prev_count = len(self.data["previous_session"])
                curr_count = len(self.data["current_session"])
                print(f"📂 Loaded: {curr_count} current, {prev_count} previous")
                
            except (json.JSONDecodeError, TypeError) as e:
                print(f"⚠️ Gagal load data: {e}")
                self.data = {"previous_session": {}, "current_session": {}}
        else:
            print("📂 Memulai tracking baru...")
    
    def _save_data(self):
        """Save our tracking data to a file (like writing in a diary)"""
        try:
            with open(self.activity_file, 'w', encoding='utf-8') as f:
                json.dump(self.data, f, ensure_ascii=False, indent=4)
        except IOError as e:
            print(f"❌ Gagal menyimpan: {e}")
    
    def _transfer_session_data(self):
        """
        Transfer session data on stop (same logic as original version)
        If current_session >= threshold: REPLACE previous_session
        If current_session < threshold: MERGE into previous_session
        """
        current_session_files = self.data["current_session"]
        current_count = len(current_session_files)
        
        if current_count >= self.rotation_threshold:
            # REPLACE: Previous session completely overwritten
            print(f"🔄 Rotation: {current_count} files ≥ threshold ({self.rotation_threshold})")
            print("   → Replacing previous session with current session")
            self.data["previous_session"] = current_session_files.copy()
        else:
            # MERGE: Add current files to previous session
            print(f"🔄 Merge: {current_count} files < threshold ({self.rotation_threshold})")
            print("   → Merging current into previous session")
            self.data["previous_session"].update(current_session_files)
        
        # Clear current session
        self.data["current_session"] = {}
        
        # Save the rotation
        self._save_data()
        print("✅ Session data transferred")
    
    def _initial_scan(self):
        """
        Look at all shortcuts that already exist in Recent folder
        This is our "baseline" - so we know what's NEW later
        """
        print("🔍 Scanning folder Recent...")
        
        # Look at every .lnk file (shortcut) in the Recent folder
        for shortcut_file in self.recent_folder.glob("*.lnk"):
            try:
                # Get when this shortcut was last changed
                mtime = shortcut_file.stat().st_mtime
                # Remember it: "I've seen this one before at this time"
                self.known_shortcuts[str(shortcut_file)] = mtime
            except (OSError, FileNotFoundError):
                continue
        
        print(f"📋 Baseline: {len(self.known_shortcuts)} file di Recent folder")
    
    def _resolve_shortcut(self, shortcut_path: str) -> Optional[str]:
        """
        Ask Windows: "This shortcut points to which actual file?"
        Like reading the address on an envelope to find the real house
        """
        try:
            # PowerShell command to read where the shortcut points
            ps_command = f'''
            $shell = New-Object -ComObject WScript.Shell
            $shortcut = $shell.CreateShortcut("{shortcut_path}")
            $shortcut.TargetPath
            '''
            
            # Run the PowerShell command (talk to Windows)
            result = subprocess.run(
                ['powershell', '-Command', ps_command],
                capture_output=True,
                text=True,
                creationflags=subprocess.CREATE_NO_WINDOW  # Run silently
            )
            
            # Get the answer: the real file path
            target_path = result.stdout.strip()
            return target_path if target_path else None
            
        except Exception:
            return None
    
    def _check_for_new_files(self) -> list:
        """
        Check: Are there any NEW shortcuts? (meaning: user opened a file!)
        Returns a list of newly opened files
        """
        new_files = []
        
        # Look at every shortcut in Recent folder
        for shortcut_file in self.recent_folder.glob("*.lnk"):
            shortcut_path = str(shortcut_file)
            
            try:
                # When was this shortcut last changed?
                mtime = shortcut_file.stat().st_mtime
            except (OSError, FileNotFoundError):
                continue
            
            # Is this shortcut NEW or UPDATED since we last checked?
            if shortcut_path not in self.known_shortcuts or self.known_shortcuts[shortcut_path] < mtime:
                # Find out which real file this shortcut points to
                target_path = self._resolve_shortcut(shortcut_path)
                
                # If we found a real file that exists...
                if target_path and os.path.exists(target_path):
                    # Skip folders (we only want files)
                    if not self._is_valid_file(target_path):
                        self.known_shortcuts[shortcut_path] = mtime
                        continue
                    
                    # Get just the file name (not the whole path)
                    file_name = Path(target_path).name
                    # Get current time
                    timestamp = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
                    # Add to our list of new files!
                    new_files.append((file_name, timestamp))
                
                # Remember: we've now seen this shortcut at this time
                self.known_shortcuts[shortcut_path] = mtime
        
        return new_files
    
    def record_file(self, file_name: str, timestamp: str):
        """
        Write down that a file was opened (add to current session)
        Like writing an entry in today's diary
        NO ROTATION HERE - only save
        """
        self.data["current_session"][file_name] = timestamp
        self._save_data()  # Save to file immediately
        
        # Show notification
        print(f"  📄 {file_name}")
    
    def start_tracking(self, interval: int = 2):
        """
        Start watching for opened files!
        Checks every 2 seconds (like looking out the window every 2 seconds)
        """
        print("\n" + "="*60)
        print("📁 FILE TRACKER AKTIF")
        print("="*60)
        print(f"📌 Memonitor: {self.recent_folder}")
        print(f"📌 Interval: setiap {interval} detik")
        print(f"📌 Rotation threshold: {self.rotation_threshold} files (on stop)")
        print(f"📌 Output: {self.activity_file}")
        print("📌 Tekan Ctrl+C untuk berhenti")
        print("="*60)
        print("\n⏳ Menunggu file dibuka...\n")
        
        try:
            # Loop forever (until user presses Ctrl+C)
            while True:
                # Check: any new files?
                new_files = self._check_for_new_files()
                
                # If we found new files...
                if new_files:
                    current_time = datetime.now().strftime("%H:%M:%S")
                    print(f"[{current_time}] 📂 File dibuka:")
                    
                    # Record each one
                    for file_name, timestamp in new_files:
                        self.record_file(file_name, timestamp)
                    
                    print()
                
                # Wait before checking again
                time.sleep(interval)
                
        except KeyboardInterrupt:
            # User pressed Ctrl+C, stop gracefully
            self._on_stop()
    
    def _on_stop(self):
        """Show summary when tracking stops + perform rotation"""
        print("\n\n" + "="*60)
        print("🛑 TRACKING DIHENTIKAN")
        print("="*60)
        
        current_count = len(self.data["current_session"])
        previous_count = len(self.data["previous_session"])
        
        print(f"📊 File session ini: {current_count}")
        print(f"📊 File session sebelumnya: {previous_count}")
        
        # Show last 5 files from current session
        if self.data["current_session"]:
            print("\n📋 File dibuka session ini:")
            items = list(self.data["current_session"].items())[-5:]
            for file_name, timestamp in items:
                print(f"  {file_name}")
        
        print()
        
        # Perform rotation using threshold logic
        self._transfer_session_data()
        
        print(f"💾 Disimpan ke: {self.activity_file}")
        print("\n✅ Selesai!")
    
    def print_history(self, limit: int = 20):
        """Show history of opened files (like reading your diary)"""
        print("\n" + "="*60)
        print("📋 RIWAYAT FILE DIBUKA")
        print("="*60)
        
        # Current session (today's diary)
        print(f"\n🟢 CURRENT SESSION ({len(self.data['current_session'])} file):")
        print("-"*40)
        if self.data["current_session"]:
            items = list(self.data["current_session"].items())[-limit:]
            for file_name, timestamp in items:
                print(f"  {timestamp}  {file_name}")
        else:
            print("  (kosong)")
        
        # Previous session (yesterday's diary)
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
        """Delete all history (erase the whole diary)"""
        self.data = {"previous_session": {}, "current_session": {}}
        self._save_data()
        print("✅ Semua riwayat telah dihapus!")
    
    def clear_previous(self):
        """Delete only previous session (erase yesterday's pages)"""
        self.data["previous_session"] = {}
        self._save_data()
        print("✅ Previous session telah dihapus!")


# ============================================================================
# Main Entry Point - Where the program starts
# ============================================================================

def main():
    """Main entry point"""
    import argparse
    
    # Set up command options (like menu choices)
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
    parser.add_argument('-t', '--threshold', type=int, default=3,
                        help='Rotation threshold: berapa file di current sebelum rotate (default: 3)')
    
    args = parser.parse_args()
    
    # Show welcome message
    print("""
╔═══════════════════════════════════════════════════════════════╗
║                    FILE TRACKER v2.1.1                        ║
║         Melacak File yang Dibuka oleh User                    ║
╚═══════════════════════════════════════════════════════════════╝
    """)
    
    # Create the tracker
    tracker = FileTracker(rotation_threshold=args.threshold)
    
    # Do what the user asked for
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