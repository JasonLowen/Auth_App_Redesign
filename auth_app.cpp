#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <random>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <iomanip>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

HWND g_hwnd = NULL;
HHOOK g_hKeyboardHook = NULL;
HHOOK g_hMouseHook = NULL;
HWND g_hMainWindow = NULL;
std::atomic<bool> g_authSuccessful(false);
std::atomic<bool> g_gestureBlockerRunning(false);
std::atomic<bool> g_firstTimeSetupMode(false); // Flag untuk first time setup
std::thread g_gestureBlockerThread;
std::vector<HWND> g_checkboxes;
HWND g_submitButton;
HWND g_forgotButton;
HWND g_labelWindow;
HWND g_backupKeyEdit = NULL;      // Edit box untuk input backup key
HWND g_verifyKeyButton = NULL;    // Button untuk verify backup key
HWND g_cancelKeyButton = NULL;    // Button untuk cancel input
HWND g_keyInputLabel = NULL;      // Label untuk input backup key
bool g_forgotFilesMode = false;   // Flag untuk mode forgot files
std::vector<std::string> g_recentFiles;
std::vector<std::string> g_correctFiles;
std::vector<std::string> g_challengeFiles;
const std::string BACKUP_KEY_FILE = "backup_key.dat";
const int TOTAL_BACKUP_KEYS = 10;

// Structure untuk menyimpan backup key dengan status
struct BackupKeyEntry {
    int index;
    std::string hashedKey;
    bool used;
};

std::vector<BackupKeyEntry> g_backupKeys;

// Dialog globals (forward declaration)
static HWND g_hDialogEdit = NULL;
static bool g_dialogClosed = false;
static int g_verifiedKeyIndex = -1; // Index key yang berhasil diverifikasi
static bool g_markKeyAfterUse = true; // Flag untuk mark key setelah digunakan

std::vector<std::string> ParseJsonFiles(const std::string &content)
{
    std::vector<std::string> files;

    size_t prevSessionPos = content.find("\"previous_session\"");
    if (prevSessionPos == std::string::npos)
    {
        return files;
    }

    size_t openBrace = content.find("{", prevSessionPos);
    if (openBrace == std::string::npos)
    {
        return files;
    }

    int braceCount = 1;
    size_t pos = openBrace + 1;
    size_t closeBrace = std::string::npos;

    while (pos < content.length() && braceCount > 0)
    {
        if (content[pos] == '{')
            braceCount++;
        else if (content[pos] == '}')
        {
            braceCount--;
            if (braceCount == 0)
            {
                closeBrace = pos;
                break;
            }
        }
        pos++;
    }

    if (closeBrace == std::string::npos)
    {
        return files;
    }

    std::string section = content.substr(openBrace + 1, closeBrace - openBrace - 1);

    size_t searchPos = 0;
    while (true)
    {
        size_t quoteStart = section.find("\"", searchPos);
        if (quoteStart == std::string::npos)
            break;

        size_t quoteEnd = section.find("\"", quoteStart + 1);
        if (quoteEnd == std::string::npos)
            break;

        std::string filename = section.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

        size_t colonPos = section.find(":", quoteEnd);
        if (colonPos != std::string::npos)
        {
            bool isKey = true;
            for (size_t i = quoteEnd + 1; i < colonPos; i++)
            {
                if (section[i] != ' ' && section[i] != '\t' && section[i] != '\n' && section[i] != '\r')
                {
                    if (section[i] != ':')
                    {
                        isKey = false;
                        break;
                    }
                }
            }

            if (isKey && !filename.empty())
            {
                files.push_back(filename);
            }
        }

        searchPos = quoteEnd + 1;
    }

    return files;
}

// Backup Key Management Functions
std::string GenerateRandomBackupKey(int length = 16)
{
    // Menggunakan charset yang lebih mudah dibaca/diketik (tanpa karakter ambigu)
    const char charset[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, strlen(charset) - 1);
    
    std::string key;
    for (int i = 0; i < length; i++)
    {
        if (i > 0 && i % 4 == 0) key += "-"; // Format: XXXX-XXXX-XXXX-XXXX
        key += charset[dis(gen)];
    }
    return key;
}

std::string SimpleHash(const std::string &input)
{
    unsigned long hash = 5381;
    for (char c : input)
    {
        hash = ((hash << 5) + hash) + c;
    }
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return ss.str();
}

// Save semua backup keys ke file
bool SaveAllBackupKeys(const std::vector<std::pair<std::string, bool>>& keys)
{
    try
    {
        std::ofstream file(BACKUP_KEY_FILE);  // Tidak pakai binary mode
        if (file.is_open())
        {
            file << "SETUP_COMPLETE=1\n";
            for (size_t i = 0; i < keys.size(); i++)
            {
                std::string hashed = SimpleHash(keys[i].first);
                std::string status = keys[i].second ? "USED" : "UNUSED";
                file << "KEY:" << i << ":" << status << ":" << hashed << "\n";
            }
            file.close();
            return true;
        }
    }
    catch (...)
    {
        return false;
    }
    return false;
}

// Save backup keys dari vector BackupKeyEntry (untuk update status) (Add On)
bool SaveBackupKeyEntries()
{
    try
    {
        std::ofstream file(BACKUP_KEY_FILE);  
        if (file.is_open())
        {
            file << "SETUP_COMPLETE=1\n";
            for (const auto& entry : g_backupKeys)
            {
                std::string status = entry.used ? "USED" : "UNUSED";
                file << "KEY:" << entry.index << ":" << status << ":" << entry.hashedKey << "\n";
            }
            file.close();
            return true;
        }
    }
    catch (...)
    {
        return false;
    }
    return false;
}

// Helper function untuk trim whitespace dan carriage return (Add On)
std::string TrimString(const std::string& str)
{
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// Load semua backup keys dari file (Add On)
bool LoadAllBackupKeys()
{
    g_backupKeys.clear();
    try
    {
        std::ifstream file(BACKUP_KEY_FILE);
        if (!file.is_open())
        {
            return false;
        }
        
        std::string line;
        bool setupComplete = false; 
        
        while (std::getline(file, line))
        {
            line = TrimString(line);
            
            if (line.empty()) continue;
            
            if (line.find("SETUP_COMPLETE=1") != std::string::npos)
            {
                setupComplete = true;
                continue;
            }
            
            if (line.find("KEY:") == 0)
            {
                // Parse format: KEY:index:status:hash (3 colons total)
                size_t firstColon = line.find(":", 4);
                size_t secondColon = line.find(":", firstColon + 1);
                
                if (firstColon != std::string::npos && 
                    secondColon != std::string::npos)
                {
                    BackupKeyEntry entry;
                    entry.index = std::stoi(line.substr(4, firstColon - 4));
                    std::string status = line.substr(firstColon + 1, secondColon - firstColon - 1);
                    entry.used = (status == "USED");
                    entry.hashedKey = TrimString(line.substr(secondColon + 1));
                    g_backupKeys.push_back(entry);
                }
            }
        }
        file.close();
        
        return setupComplete && !g_backupKeys.empty();
    }
    catch (...)
    {
        return false;
    }
    return false;
}

// Helper untuk compare hash (case-insensitive dan trim) (Add On)
bool CompareHash(const std::string& hash1, const std::string& hash2)
{
    std::string h1 = TrimString(hash1);
    std::string h2 = TrimString(hash2);
    
    // Convert both to lowercase for comparison
    std::transform(h1.begin(), h1.end(), h1.begin(), ::tolower);
    std::transform(h2.begin(), h2.end(), h2.begin(), ::tolower);
    
    return h1 == h2;
}

// Verify backup key dan return index jika valid (-1 jika invalid) (Add on)
int VerifyBackupKey(const std::string &inputKey)
{
    // Hash input langsung tanpa modifikasi apapun
    std::string inputHash = SimpleHash(inputKey);
    
    // Loop semua backup keys dan bandingkan hash
    for (size_t i = 0; i < g_backupKeys.size(); i++)
    {
        if (CompareHash(g_backupKeys[i].hashedKey, inputHash))
        {
            return (int)i;  // Kembalikan index jika cocok
        }
    }
    
    return -1;  // Tidak ditemukan
}

// Check apakah key sudah digunakan (Add on)
bool IsKeyUsed(int keyIndex)
{
    if (keyIndex >= 0 && keyIndex < (int)g_backupKeys.size())
    {
        return g_backupKeys[keyIndex].used;
    }
    return true; // Default anggap sudah digunakan jika index invalid
}

// Mark key sebagai sudah digunakan (Add on)
bool MarkKeyAsUsed(int keyIndex)
{
    if (keyIndex >= 0 && keyIndex < (int)g_backupKeys.size())
    {
        g_backupKeys[keyIndex].used = true;
        return SaveBackupKeyEntries();
    }
    return false;
}

// Hitung berapa key yang masih tersedia
int GetAvailableKeyCount()
{
    int count = 0;
    for (const auto& entry : g_backupKeys)
    {
        if (!entry.used) count++;
    }
    return count;
}

// Check apakah ini first time setup (belum ada backup_key.dat atau belum complete)
bool IsFirstTimeSetup()
{
    std::ifstream file(BACKUP_KEY_FILE);
    if (!file.good()) return true;
    
    std::string line;
    while (std::getline(file, line))
    {
        if (line.find("SETUP_COMPLETE=1") != std::string::npos)
        {
            file.close();
            return false;
        }
    }
    file.close();
    return true;
}

// Check apakah backup key file exists dan valid
bool BackupKeyExists()
{
    return !IsFirstTimeSetup();
}

bool IsAdmin();
bool ElevateProcess();
std::vector<std::string> LoadFilesFromJson();
std::vector<std::string> GetIncorrectFiles();
std::vector<std::string> GetRandomCorrectFiles(const std::vector<std::string> &files, int count);
std::vector<std::string> GenerateChallengeFiles(const std::vector<std::string> &correct);
void DisableTaskManager();
void EnableTaskManager();
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void GestureBlockerThread();
void Cleanup();
bool VerifySelection();
void ShowForgotFilesDialog();
void ShowUpdateBackupKeyDialog();
bool ShowFirstTimeSetupDialog();
bool ShowFirstTimeLoginDialog();
bool ShowInputKeyDialog(const char* title, const char* prompt, char* outputBuffer, int bufferSize);

bool IsAdmin()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup))
    {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }

    return isAdmin != FALSE;
}

bool ElevateProcess()
{
    if (IsAdmin())
    {
        std::cout << "Running with administrator privileges.\n";
        return true;
    }

    std::cout << "This application requires administrator privileges.\n";
    std::cout << "Attempting to restart with elevation...\n";

    wchar_t szPath[MAX_PATH];
    if (GetModuleFileNameW(NULL, szPath, MAX_PATH) == 0)
    {
        return false;
    }

    SHELLEXECUTEINFOW sei = {sizeof(SHELLEXECUTEINFOW)};
    sei.lpVerb = L"runas";
    sei.lpFile = szPath;
    sei.hwnd = NULL;
    sei.nShow = SW_NORMAL;

    if (!ShellExecuteExW(&sei))
    {
        DWORD error = GetLastError();
        if (error != ERROR_CANCELLED)
        {
            MessageBoxW(NULL, L"Failed to elevate privileges", L"Error", MB_ICONERROR);
        }
        return false;
    }

    ExitProcess(0);
    return true;
}

std::vector<std::string> LoadFilesFromJson()
{
    std::vector<std::string> files;
    std::vector<std::string> fileDefault;
    fileDefault = {"document1.txt", "document2.txt", "document3.txt"};
    try
    {
        std::ifstream file("file_activity.json");
        if (file.is_open())
        {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            file.close();

            files = ParseJsonFiles(content);
        }
    }
    catch (...)
    {
        std::cout << "Error loading files from JSON\n";
    }

    if (files.size() < 3)
    {
        for (int i = files.size(); i < 3; i++)
        {
            files.push_back(fileDefault[i]);
        }
    }

    return files;
}

//Adds - On
std::vector<std::string> GetIncorrectFiles()
{
    static std::vector<std::string> first = {
    "Report","report", "Proposal","proposal", "Invoice","invoice", 
    "Summary","summary", "Presentation","presentation",
    "Contract", "Documentation", "specification", "Analysis", "Plan",
    "Overview", "Brief", "statement", "Record", "Outline",
    "Blueprint", "Assessment", "evaluation","Evaluation", "Guide", "Checklist",
    "Portfolio", "Workflow", "Timeline", "Roadmap", "Memo",
    "Schedule", "Profile", "Dataset", "Notes","notes", "requirements","Requirements"
    };
   static std::vector<std::string> second = {
    "Meeting","meeting", "Monthly","monthly", "Annual", 
    "Final","final", "Draft","draft", "Revision","revision",
    "Backup", "backup", "Internal","internal", "Client",
    "Testing", "Production","client","production"
    "Development", "Preliminary", "Updated", "Official", "Version1",
    "Version2", "Prototype", "Temporary", "archive", "General",
    "confidential", "External", "Pending","pending", "Submitted", "Approved",
    "inProgress","InProgress", "FollowUp", "Review", "Validation", "Research",
    "research","development"
    };

    static std::vector<std::string> extensions = {
        ".pdf", ".docx", ".xlsx", ".pptx", ".txt",
        ".csv", ".json", ".xml", ".js", ".py" ,".php", 
        ".css", ".exe",".png",".jpeg",".jpg",".mp4",
        ".mp3", ".zip", ".html", ".tsx",".jsx",".ts",
        ".sql"
    };

    static std::mt19937 gen(std::random_device{}());

    std::uniform_int_distribution<> d1(0, first.size() - 1);
    std::uniform_int_distribution<> d2(0, second.size() - 1);
    std::uniform_int_distribution<> d3(0, extensions.size() - 1);

    int FILE_COUNT = 20;

    std::vector<std::string> files;
    files.reserve(FILE_COUNT);

    for (int i = 0; i < FILE_COUNT; ++i)
    {
        files.push_back(
            first[d1(gen)] + "_" +
            second[d2(gen)] +
            extensions[d3(gen)]
        );
    }

    return files;
}

std::vector<std::string> GetRandomCorrectFiles(const std::vector<std::string> &files, int count)
{
    if ((int)files.size() < count)
    {
        return files;
    }

    std::vector<std::string> result = files;
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(result.begin(), result.end(), g);
    result.resize(count);
    return result;
}

std::vector<std::string> GenerateChallengeFiles(const std::vector<std::string> &correct)
{
    std::vector<std::string> incorrect = GetIncorrectFiles();
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(incorrect.begin(), incorrect.end(), g);

    std::vector<std::string> selected(incorrect.begin(), incorrect.begin() + 6);
    std::vector<std::string> combined = correct;
    combined.insert(combined.end(), selected.begin(), selected.end());
    std::shuffle(combined.begin(), combined.end(), g);

    return combined;
}

void DisableTaskManager()
{
    HKEY hKey;
    DWORD value = 1;

    if (RegCreateKeyExA(HKEY_CURRENT_USER,
                        "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExA(hKey, "DisableTaskMgr", 0, REG_DWORD,
                       (BYTE *)&value, sizeof(DWORD));
        RegCloseKey(hKey);
        std::cout << "Task Manager disabled\n";
    }
}

void EnableTaskManager()
{
    HKEY hKey;
    DWORD value = 0;

    if (RegCreateKeyExA(HKEY_CURRENT_USER,
                        "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExA(hKey, "DisableTaskMgr", 0, REG_DWORD,
                       (BYTE *)&value, sizeof(DWORD));
        RegCloseKey(hKey);
        std::cout << "Task Manager re-enabled\n";
    }
}

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    // If hook is NULL, allow all input (dialog is active)
    if (g_hKeyboardHook == NULL)
    {
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }
    
    // PENTING: Jika dalam mode forgot files, allow semua keyboard input
    if (g_forgotFilesMode)
    {
        KBDLLHOOKSTRUCT *pKbd = (KBDLLHOOKSTRUCT *)lParam;
        bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        
        // Block hanya system shortcuts
        if ((altPressed && pKbd->vkCode == VK_F4) ||
            (altPressed && pKbd->vkCode == VK_TAB) ||
            (pKbd->vkCode == VK_LWIN || pKbd->vkCode == VK_RWIN) ||
            (ctrlPressed && pKbd->vkCode == VK_ESCAPE) ||
            (ctrlPressed && altPressed && pKbd->vkCode == VK_DELETE))
        {
            return 1;
        }
        // Allow semua keyboard lainnya untuk typing
        return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
    }
    
    if (nCode >= 0 && !g_authSuccessful)
    {
        KBDLLHOOKSTRUCT *pKbd = (KBDLLHOOKSTRUCT *)lParam;

        bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

        if (pKbd->vkCode == VK_TAB ||
            pKbd->vkCode == VK_UP ||
            pKbd->vkCode == VK_DOWN ||
            pKbd->vkCode == VK_LEFT ||
            pKbd->vkCode == VK_RIGHT ||
            pKbd->vkCode == VK_SPACE ||
            pKbd->vkCode == VK_RETURN)
        {
            return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
        }

        if ((altPressed && pKbd->vkCode == VK_F4) ||
            (altPressed && pKbd->vkCode == VK_TAB) ||
            (pKbd->vkCode == VK_LWIN || pKbd->vkCode == VK_RWIN) ||
            (ctrlPressed && pKbd->vkCode == VK_ESCAPE) ||
            (ctrlPressed && altPressed && pKbd->vkCode == VK_DELETE))
        {
            return 1;
        }
    }

    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && !g_authSuccessful)
    {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP || wParam == WM_LBUTTONDBLCLK)
        {
            MSLLHOOKSTRUCT *pMouse = (MSLLHOOKSTRUCT *)lParam;
            POINT clickPos = pMouse->pt;

            HWND hwndAtPoint = WindowFromPoint(clickPos);

            HWND parentWnd = hwndAtPoint;
            while (parentWnd != NULL)
            {
                if (parentWnd == g_hMainWindow)
                {

                    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
                }
                parentWnd = GetParent(parentWnd);
            }

            if (hwndAtPoint == g_submitButton || 
                hwndAtPoint == g_forgotButton)
            {
                return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
            }

            for (HWND cb : g_checkboxes)
            {
                if (hwndAtPoint == cb)
                {
                    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
                }
            }

            return 1;
        }
    }

    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

void GestureBlockerThread()
{
    while (g_gestureBlockerRunning)
    {
        HWND hwnd = GetForegroundWindow();
        if (hwnd != NULL && hwnd != g_hMainWindow)
        {
            char className[256] = {0};
            char windowTitle[256] = {0};

            GetClassNameA(hwnd, className, sizeof(className));
            GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));

            std::string classStr(className);
            std::string titleStr(windowTitle);

            std::transform(classStr.begin(), classStr.end(), classStr.begin(), ::tolower);
            std::transform(titleStr.begin(), titleStr.end(), titleStr.begin(), ::tolower);

            bool isBlocked = false;

            std::vector<std::string> blockedClasses = {
                "multitaskingviewframe",
                "windows.ui.core.corewindow",
                "taskswitcherwnd",
                "foregroundstaging"};

            for (const auto &blocked : blockedClasses)
            {
                if (classStr.find(blocked) != std::string::npos)
                {
                    isBlocked = true;
                    break;
                }
            }

            std::vector<std::string> blockedTitles = {
                "task view",
                "timeline",
                "task switching"};

            for (const auto &blocked : blockedTitles)
            {
                if (titleStr.find(blocked) != std::string::npos)
                {
                    isBlocked = true;
                    break;
                }
            }

            if (isBlocked)
            {
                std::cout << "Detected gesture window: " << className << " - " << windowTitle << "\n";
                PostMessage(hwnd, WM_CLOSE, 0, 0);
                SetForegroundWindow(g_hMainWindow);
                SetWindowPos(g_hMainWindow, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool VerifySelection()
{
    std::set<std::string> selected;

    for (size_t i = 0; i < g_checkboxes.size(); i++)
    {
        if (SendMessage(g_checkboxes[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
        {
            selected.insert(g_challengeFiles[i]);
        }
    }

    // Store hook state
    HHOOK savedKeyboardHook = g_hKeyboardHook;
    HHOOK savedMouseHook = g_hMouseHook;
    
    // COMPLETELY disable hooks for MessageBox
    if (g_hKeyboardHook != NULL)
    {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = NULL;
    }
    if (g_hMouseHook != NULL)
    {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = NULL;
    }
    
    Sleep(50);

    if (selected.size() != 3)
    {
        MessageBoxA(g_hMainWindow, "Please select exactly 3 files.\nTry again.",
                    "Access Denied", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        
        // Re-enable hooks
        if (savedKeyboardHook != NULL)
        {
            g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc,
                                              GetModuleHandle(NULL), 0);
        }
        if (savedMouseHook != NULL)
        {
            g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc,
                                           GetModuleHandle(NULL), 0);
        }

        HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        int panelWidth = 600;
        int startX = (screenWidth - panelWidth) / 2;
        int yPos = screenHeight / 4;

        for(HWND chk : g_checkboxes){
            DestroyWindow(chk);
        }
        g_checkboxes.clear();
        
        g_correctFiles = GetRandomCorrectFiles(g_recentFiles, 3);
        g_challengeFiles = GenerateChallengeFiles(g_correctFiles);

        static HFONT hLabelFont = NULL;

        hLabelFont = CreateFont(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        DestroyWindow(g_labelWindow);

        g_labelWindow = CreateWindowExA(0, "STATIC", "Select the 3 files you worked on recently:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            startX, yPos, panelWidth - 100, 30,
            g_hMainWindow, NULL, GetModuleHandle(NULL), NULL);
            SendMessage(g_labelWindow, WM_SETFONT, (WPARAM)hLabelFont, TRUE);

        yPos += 50;

        for (const auto &file : g_challengeFiles)
        {
            HWND checkbox = CreateWindowExA(0, "BUTTON", file.c_str(),
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                            startX, yPos, panelWidth - 100, 30,
                                            g_hMainWindow,NULL, GetModuleHandle(NULL), NULL);
            SendMessage(checkbox, WM_SETFONT, (WPARAM)hFont, TRUE);
            g_checkboxes.push_back(checkbox);
            yPos += 35; 
        }

        SetFocus(g_checkboxes[0]);
        
        ShowWindow(g_hMainWindow, SW_MAXIMIZE);
        UpdateWindow(g_hMainWindow);
        return false;
    }

    std::set<std::string> correct(g_correctFiles.begin(), g_correctFiles.end());

    if (selected == correct)
    {
        // Re-enable hooks before returning true
        if (savedKeyboardHook != NULL)
        {
            g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc,
                                              GetModuleHandle(NULL), 0);
        }
        if (savedMouseHook != NULL)
        {
            g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc,
                                           GetModuleHandle(NULL), 0);
        }
        return true;
    }
    else
    {
        MessageBoxA(g_hMainWindow, "Incorrect challenge response.\nPlease try again.",
                    "Access Denied", MB_OK | MB_ICONWARNING | MB_TOPMOST);


        // Re-enable hooks
        if (savedKeyboardHook != NULL)
        {
            g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc,
                                              GetModuleHandle(NULL), 0);
        }
        if (savedMouseHook != NULL)
        {
            g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc,
                                           GetModuleHandle(NULL), 0);
        }

        HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        int panelWidth = 600;
        int startX = (screenWidth - panelWidth) / 2;
        int yPos = screenHeight / 4;

        for(HWND chk : g_checkboxes){
            DestroyWindow(chk);
        }
        g_checkboxes.clear();
        

        g_correctFiles = GetRandomCorrectFiles(g_recentFiles, 3);
        g_challengeFiles = GenerateChallengeFiles(g_correctFiles);

        static HFONT hLabelFont = NULL;

        hLabelFont = CreateFont(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        DestroyWindow(g_labelWindow);

        g_labelWindow = CreateWindowExA(0, "STATIC", "Select the 3 files you worked on recently:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            startX, yPos, panelWidth - 100, 30,
            g_hMainWindow, NULL, GetModuleHandle(NULL), NULL);
            SendMessage(g_labelWindow, WM_SETFONT, (WPARAM)hLabelFont, TRUE);

        yPos += 50;

        for (const auto &file : g_challengeFiles)
        {
            HWND checkbox = CreateWindowExA(0, "BUTTON", file.c_str(),
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                            startX, yPos, panelWidth - 100, 30,
                                            g_hMainWindow,NULL, GetModuleHandle(NULL), NULL);
            SendMessage(checkbox, WM_SETFONT, (WPARAM)hFont, TRUE);
            g_checkboxes.push_back(checkbox);
            yPos += 35; 
        }

        SetFocus(g_checkboxes[0]);
        
        ShowWindow(g_hMainWindow, SW_MAXIMIZE);
        UpdateWindow(g_hMainWindow);

        return false;
    }
}

// Dialog procedure for backup key input
LRESULT CALLBACK BackupKeyDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        
        HFONT hLabelFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        
        SetProp(hwnd, "NormalFont", hFont);
        SetProp(hwnd, "LabelFont", hLabelFont);
        
        // Label
        HWND hLabel = CreateWindowExA(0, "STATIC", "Enter your backup key to reveal the correct files:",
                                      WS_CHILD | WS_VISIBLE | SS_LEFT,
                                      20, 20, 460, 25,
                                      hwnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(hLabel, WM_SETFONT, (WPARAM)hLabelFont, TRUE);
        
        // Edit box for key input (tidak pakai ES_PASSWORD agar user bisa lihat input)
        g_hDialogEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE | WS_TABSTOP,
                                       20, 55, 460, 30,
                                       hwnd, (HMENU)1001, GetModuleHandle(NULL), NULL);
        SendMessage(g_hDialogEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_hDialogEdit, EM_SETLIMITTEXT, 64, 0); // Max 64 chars
        
        // Verify button
        HWND hVerifyBtn = CreateWindowExA(0, "BUTTON", "Verify Key",
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                         160, 105, 100, 35,
                                         hwnd, (HMENU)1002, GetModuleHandle(NULL), NULL);
        SendMessage(hVerifyBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        // Cancel button
        HWND hCancelBtn = CreateWindowExA(0, "BUTTON", "Cancel",
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                         270, 105, 100, 35,
                                         hwnd, (HMENU)1003, GetModuleHandle(NULL), NULL);
        SendMessage(hCancelBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        g_dialogClosed = false;
        return 0;
    }
    
    case WM_SHOWWINDOW:
        if (wParam && g_hDialogEdit)
        {
            SetFocus(g_hDialogEdit);
        }
        return 0;
    
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && g_hDialogEdit)
        {
            SetFocus(g_hDialogEdit);
        }
        return 0;
    
    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 1002) // Verify button clicked
        {
            char inputKey[256] = {0};
            GetWindowTextA(g_hDialogEdit, inputKey, sizeof(inputKey));
            
            if (strlen(inputKey) == 0)
            {
                MessageBoxA(hwnd, "Please enter your backup key!", "Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
                SetFocus(g_hDialogEdit);
                return 0;
            }
            
            // Verifikasi key dengan sistem multi-key
            int keyIndex = VerifyBackupKey(inputKey);
            
            if (keyIndex >= 0)
            {
                // Key valid, check apakah sudah digunakan
                if (IsKeyUsed(keyIndex))
                {
                    std::stringstream errMsg;
                    errMsg << "This backup key has already been used!\n\n";
                    errMsg << "Key #" << (keyIndex + 1) << " was previously used.\n";
                    errMsg << "Please use a different backup key.\n\n";
                    errMsg << "Remaining available keys: " << GetAvailableKeyCount() << " of " << TOTAL_BACKUP_KEYS;
                    
                    MessageBoxA(hwnd, errMsg.str().c_str(), 
                               "Key Already Used", MB_OKCANCEL | MB_ICONWARNING | MB_TOPMOST);
                    SetWindowTextA(g_hDialogEdit, "");
                    SetFocus(g_hDialogEdit);
                    return 0;
                }
                
                // Key valid dan belum digunakan
                g_verifiedKeyIndex = keyIndex;
                
                // Mark key sebagai used jika flag aktif
                if (g_markKeyAfterUse)
                {
                    MarkKeyAsUsed(keyIndex);
                }
                
                SetProp(hwnd, "VerificationResult", (HANDLE)1);
                g_dialogClosed = true;
                DestroyWindow(hwnd);
            }
            else
            {
                std::stringstream errMsg;
                errMsg << "Invalid backup key!\n\n";
                errMsg << "Please check your key and try again.\n";
                errMsg << "Keys are case-sensitive.\n\n";
                errMsg << "Remaining available keys: " << GetAvailableKeyCount() << " of " << TOTAL_BACKUP_KEYS;
                
                MessageBoxA(hwnd, errMsg.str().c_str(), 
                           "Access Denied", MB_OK | MB_ICONERROR | MB_TOPMOST);
                SetWindowTextA(g_hDialogEdit, "");
                SetFocus(g_hDialogEdit);
            }
            return 0;
        }
        else if (LOWORD(wParam) == 1003) // Cancel button clicked
        {
            SetProp(hwnd, "VerificationResult", (HANDLE)0);
            g_dialogClosed = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    
    case WM_CLOSE:
        SetProp(hwnd, "VerificationResult", (HANDLE)0);
        g_dialogClosed = true;
        DestroyWindow(hwnd);
        return 0;
    
    case WM_DESTROY:
    {
        HFONT hFont = (HFONT)GetProp(hwnd, "NormalFont");
        HFONT hLabelFont = (HFONT)GetProp(hwnd, "LabelFont");
        if (hFont) DeleteObject(hFont);
        if (hLabelFont) DeleteObject(hLabelFont);
        RemoveProp(hwnd, "NormalFont");
        RemoveProp(hwnd, "LabelFont");
        g_hDialogEdit = NULL;
        g_dialogClosed = true;
        PostQuitMessage(0);
        return 0;
    }
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Fungsi untuk menampilkan input box backup key di main window
void ShowBackupKeyInput()
{
    if (!BackupKeyExists())
    {
        MessageBoxA(g_hMainWindow,
                   "No backup keys found!\n\nPlease set up backup keys first.",
                   "Backup Keys Required",
                   MB_OK | MB_ICONWARNING);
        return;
    }
    
    int availableKeys = GetAvailableKeyCount();
    if (availableKeys == 0)
    {
        MessageBoxA(g_hMainWindow,
                   "All backup keys have been used!\n\n"
                   "You have no more recovery options available.",
                   "No Keys Available", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Enable forgot files mode - ini akan enable keyboard
    g_forgotFilesMode = true;
    
    // Update label
    std::stringstream labelText;
    labelText << "Enter backup key (Available: " << availableKeys << "/" << TOTAL_BACKUP_KEYS << "):";
    SetWindowTextA(g_keyInputLabel, labelText.str().c_str());
    
    // Show input controls
    ShowWindow(g_keyInputLabel, SW_SHOW);
    ShowWindow(g_backupKeyEdit, SW_SHOW);
    ShowWindow(g_verifyKeyButton, SW_SHOW);
    ShowWindow(g_cancelKeyButton, SW_SHOW);
    
    // Hide other controls temporarily
    ShowWindow(g_labelWindow, SW_HIDE);
    for (HWND cb : g_checkboxes)
    {
        ShowWindow(cb, SW_HIDE);
    }
    ShowWindow(g_submitButton, SW_HIDE);
    ShowWindow(g_forgotButton, SW_HIDE);
    
    // Clear and focus edit box
    SetWindowTextA(g_backupKeyEdit, "");
    SetFocus(g_backupKeyEdit);
}

// Fungsi untuk menyembunyikan input box dan kembali ke tampilan normal
void HideBackupKeyInput()
{
    g_forgotFilesMode = false;
    
    // Hide input controls
    ShowWindow(g_keyInputLabel, SW_HIDE);
    ShowWindow(g_backupKeyEdit, SW_HIDE);
    ShowWindow(g_verifyKeyButton, SW_HIDE);
    ShowWindow(g_cancelKeyButton, SW_HIDE);
    
    // Show normal controls
    ShowWindow(g_labelWindow, SW_SHOW);
    for (HWND cb : g_checkboxes)
    {
        ShowWindow(cb, SW_SHOW);
    }
    ShowWindow(g_submitButton, SW_SHOW);
    ShowWindow(g_forgotButton, SW_SHOW);
    
    // Return focus to first checkbox
    if (!g_checkboxes.empty())
    {
        SetFocus(g_checkboxes[0]);
    }
}

// Fungsi untuk verify backup key yang diinput
void VerifyInputBackupKey()
{
    char inputKey[256] = {0};
    GetWindowTextA(g_backupKeyEdit, inputKey, sizeof(inputKey));
    
    std::string keyStr = TrimString(inputKey);
    
    if (keyStr.empty())
    {
        MessageBoxA(g_hMainWindow, "Please enter a backup key!", "Error", MB_OK | MB_ICONWARNING);
        SetFocus(g_backupKeyEdit);
        return;
    }
    
    int keyIndex = VerifyBackupKey(keyStr);
    
    if (keyIndex >= 0)
    {
        if (IsKeyUsed(keyIndex))
        {
            std::stringstream errMsg;
            errMsg << "Key #" << (keyIndex + 1) << " has already been used!\n\n";
            errMsg << "Please use a different backup key.\n";
            errMsg << "Available keys: " << GetAvailableKeyCount() << " of " << TOTAL_BACKUP_KEYS;
            MessageBoxA(g_hMainWindow, errMsg.str().c_str(), "Key Already Used", MB_OK | MB_ICONWARNING);
            SetWindowTextA(g_backupKeyEdit, "");
            SetFocus(g_backupKeyEdit);
        }
        else
        {
            // Key valid - mark as used
            MarkKeyAsUsed(keyIndex);
            
            // Show correct files
            std::stringstream correctFilesList;
            correctFilesList << "Backup Key Verified!\n\n";
            correctFilesList << "Key #" << (keyIndex + 1) << " used. Remaining: " << GetAvailableKeyCount() << "/" << TOTAL_BACKUP_KEYS << "\n\n";
            correctFilesList << "The correct files are:\n\n";
            for (const auto &file : g_correctFiles)
            {
                correctFilesList << "  - " << file << "\n";
            }
            correctFilesList << "\nPlease select these 3 files and click Submit.";
            
            MessageBoxA(g_hMainWindow, correctFilesList.str().c_str(),
                       "Correct Files Revealed", MB_OK | MB_ICONINFORMATION);
            
            // Hide input and return to normal view
            HideBackupKeyInput();
        }
    }
    else
    {
        MessageBoxA(g_hMainWindow, 
                   "Invalid backup key!\n\nPlease check your key and try again.",
                   "Invalid Key", MB_OK | MB_ICONERROR);
        SetWindowTextA(g_backupKeyEdit, "");
        SetFocus(g_backupKeyEdit);
    }
}

void ShowForgotFilesDialog()
{
    ShowBackupKeyInput();
}

// Fungsi untuk menampilkan First Time Setup (10 backup keys)
// Ini HANYA ditampilkan sekali saat pertama kali menjalankan aplikasi
bool ShowFirstTimeSetupDialog()
{
    // Generate 10 backup keys
    std::vector<std::string> generatedKeys;
    std::vector<std::pair<std::string, bool>> keysToSave;
    
    for (int i = 0; i < TOTAL_BACKUP_KEYS; i++)
    {
        std::string key = GenerateRandomBackupKey(16);
        generatedKeys.push_back(key);
        keysToSave.push_back({key, false}); // false = belum digunakan
    }
    
    // Create message dengan semua 10 keys
    std::stringstream message;
    message << "╔══════════════════════════════════════════════════════╗\n";
    message << "║     FIRST TIME SETUP - BACKUP KEYS GENERATED         ║\n";
    message << "╠══════════════════════════════════════════════════════╣\n";
    message << "║  IMPORTANT: TAKE A PHOTO OF THESE KEYS NOW!          ║\n";
    message << "║  This list will NEVER be shown again!                ║\n";
    message << "╠══════════════════════════════════════════════════════╣\n\n";
    
    for (int i = 0; i < TOTAL_BACKUP_KEYS; i++)
    {
        message << "  Key #" << (i + 1) << ":  " << generatedKeys[i] << "\n";
    }
    
    message << "\n╠══════════════════════════════════════════════════════╣\n";
    message << "║  • Each key can only be used ONCE                    ║\n";
    message << "║  • Use a key if you forget your recent files         ║\n";
    message << "║  • After using a key, it becomes invalid             ║\n";
    message << "║  • You have " << TOTAL_BACKUP_KEYS << " keys total - use them wisely!          ║\n";
    message << "╚══════════════════════════════════════════════════════╝\n";
    
    // Show the keys to user
    MessageBoxA(NULL, message.str().c_str(),
               "BACKUP KEYS - TAKE A PHOTO NOW!", MB_OK | MB_ICONWARNING | MB_TOPMOST);
    
    // Copy all keys to clipboard
    std::stringstream clipboardText;
    clipboardText << "BACKUP KEYS (Generated on first run)\n";
    clipboardText << "====================================\n\n";
    for (int i = 0; i < TOTAL_BACKUP_KEYS; i++)
    {
        clipboardText << "Key #" << (i + 1) << ": " << generatedKeys[i] << "\n";
    }
    clipboardText << "\nNOTE: Each key can only be used once!\n";
    
    if (OpenClipboard(NULL))
    {
        EmptyClipboard();
        std::string clipStr = clipboardText.str();
        HGLOBAL hClipboardData = GlobalAlloc(GMEM_DDESHARE, clipStr.length() + 1);
        if (hClipboardData != NULL)
        {
            char *pchData = static_cast<char*>(GlobalLock(hClipboardData));
            if (pchData != NULL)
            {
                strcpy(pchData, clipStr.c_str());
                GlobalUnlock(hClipboardData);
                SetClipboardData(CF_TEXT, hClipboardData);
            }
        }
        CloseClipboard();
        
        MessageBoxA(NULL,
                   "All backup keys have been copied to your clipboard!\n\n"
                   "You can paste them to a text file or note app.\n"
                   "But remember: TAKE A PHOTO as backup!",
                   "Keys Copied to Clipboard", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    }
    
    std::stringstream confirmMsg;
    confirmMsg << "FINAL CONFIRMATION\n";
    confirmMsg << "==================\n\n";
    confirmMsg << "Have you saved/photographed the backup keys?\n\n";
    confirmMsg << "WARNING: You will NOT be able to see these keys again!\n\n";
    confirmMsg << "Click YES to continue (keys will be saved)\n";
    confirmMsg << "Click NO to see the keys one more time";
    
    int result = MessageBoxA(NULL, confirmMsg.str().c_str(),
                            "Confirm Keys Saved", MB_YESNO | MB_ICONWARNING | MB_TOPMOST);
    
    if (result == IDNO)
    {
        // Show keys one more time
        MessageBoxA(NULL, message.str().c_str(),
                   "BACKUP KEYS - LAST CHANCE!", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        
        // Ask again
        result = MessageBoxA(NULL, 
                            "Are you sure you have saved all the backup keys?\n\n"
                            "This is your LAST chance to see them!",
                            "Final Confirmation", MB_YESNO | MB_ICONWARNING | MB_TOPMOST);
    }
    
    // Save keys to file
    if (SaveAllBackupKeys(keysToSave))
    {
        LoadAllBackupKeys();
        return true;
    }
    else
    {
        MessageBoxA(NULL,
                   "Failed to save backup keys.\nPlease try running the application again.",
                   "Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return false;
    }
}



void Cleanup()
{
    g_gestureBlockerRunning = false;
    if (g_gestureBlockerThread.joinable())
    {
        g_gestureBlockerThread.join();
    }

    if (g_hKeyboardHook)
    {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = NULL;
    }

    if (g_hMouseHook)
    {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = NULL;
    }

    EnableTaskManager();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static HFONT hFont = NULL;
    static HFONT hLabelFont = NULL;
    static HBRUSH hBackBrush = NULL;
    g_hwnd = hwnd;


    switch (uMsg)
    {
    case WM_CREATE:
    {
        hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        hLabelFont = CreateFont(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        hBackBrush = CreateSolidBrush(RGB(0, 0, 0));

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        int panelWidth = 600;
        int startX = (screenWidth - panelWidth) / 2;
        int yPos = screenHeight / 4;

        g_labelWindow = CreateWindowExA(0, "STATIC", "Select the 3 files you worked on recently:",
                                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                                        startX, yPos, panelWidth - 100, 30,
                                        hwnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_labelWindow, WM_SETFONT, (WPARAM)hLabelFont, TRUE);

        yPos += 50;

        for (const auto &file : g_challengeFiles)
        {
            HWND checkbox = CreateWindowExA(0, "BUTTON", file.c_str(),
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                            startX, yPos, panelWidth - 100, 30,
                                            hwnd, NULL, GetModuleHandle(NULL), NULL);
            SendMessage(checkbox, WM_SETFONT, (WPARAM)hFont, TRUE);
            g_checkboxes.push_back(checkbox);
            yPos += 35;
        }

        yPos += 20;

        g_submitButton = CreateWindowExA(0, "BUTTON", "Submit",
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                         startX + (panelWidth - 100) / 2 - 50, yPos, 120, 40,
                                         hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);
        SendMessage(g_submitButton, WM_SETFONT, (WPARAM)hFont, TRUE);

        yPos += 60;

        // Forgot Files button (centered)
        g_forgotButton = CreateWindowExA(0, "BUTTON", "Forgot Files?",
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                         startX + (panelWidth - 100) / 2 - 75, yPos, 150, 35,
                                         hwnd, (HMENU)2, GetModuleHandle(NULL), NULL);
        SendMessage(g_forgotButton, WM_SETFONT, (WPARAM)hFont, TRUE);

        // === BACKUP KEY INPUT CONTROLS (hidden by default) ===
        int inputY = screenHeight / 3;
        
        // Label untuk input
        g_keyInputLabel = CreateWindowExA(0, "STATIC", "Enter backup key:",
                                          WS_CHILD | SS_LEFT,  // NOT visible initially
                                          startX, inputY, panelWidth - 100, 30,
                                          hwnd, NULL, GetModuleHandle(NULL), NULL);
        SendMessage(g_keyInputLabel, WM_SETFONT, (WPARAM)hLabelFont, TRUE);
        
        // Edit box untuk input key
        g_backupKeyEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                          WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP,  // NOT visible initially
                                          startX, inputY + 40, panelWidth - 100, 35,
                                          hwnd, (HMENU)100, GetModuleHandle(NULL), NULL);
        SendMessage(g_backupKeyEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(g_backupKeyEdit, EM_SETLIMITTEXT, 64, 0);
        
        // Verify button
        g_verifyKeyButton = CreateWindowExA(0, "BUTTON", "Verify Key",
                                            WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,  // NOT visible initially
                                            startX, inputY + 90, 150, 40,
                                            hwnd, (HMENU)4, GetModuleHandle(NULL), NULL);
        SendMessage(g_verifyKeyButton, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        // Cancel button
        g_cancelKeyButton = CreateWindowExA(0, "BUTTON", "Cancel",
                                            WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,  // NOT visible initially
                                            startX + 170, inputY + 90, 150, 40,
                                            hwnd, (HMENU)5, GetModuleHandle(NULL), NULL);
        SendMessage(g_cancelKeyButton, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetFocus(g_checkboxes[0]);

        SetTimer(hwnd, 1, 50, NULL);

        g_gestureBlockerRunning = true;
        g_gestureBlockerThread = std::thread(GestureBlockerThread);

        break;
    }

    case WM_TIMER:
    {
        if (wParam == 1 && !g_authSuccessful)
        {
            HWND foreground = GetForegroundWindow();
            if (foreground != hwnd)
            {
                SetForegroundWindow(hwnd);
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            }
        }
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 1) // Submit button
        {
            if (VerifySelection())
            {
                g_authSuccessful = true;
                KillTimer(hwnd, 1);

                MessageBoxA(hwnd, "You have successfully logged in!",
                            "Access Granted", MB_OK | MB_ICONINFORMATION);

                Cleanup();
                PostQuitMessage(0);
            }
        }
        else if (LOWORD(wParam) == 2) // Forgot Files button
        {
            ShowForgotFilesDialog();
        }
        else if (LOWORD(wParam) == 4) // Verify Key button
        {
            VerifyInputBackupKey();
        }
        else if (LOWORD(wParam) == 5) // Cancel Key button
        {
            HideBackupKeyInput();
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(255, 255, 255));
        SetBkMode(hdcStatic, TRANSPARENT);
        return (LRESULT)hBackBrush;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rect;
        GetClientRect(hwnd, &rect);
        FillRect(hdc, &rect, hBackBrush);
        return 1;
    }

    case WM_CLOSE:
        if (!g_authSuccessful)
        {
            return 0;
        }

    case WM_DESTROY:
        if (hFont)
            DeleteObject(hFont);
        if (hLabelFont)
            DeleteObject(hLabelFont);
        if (hBackBrush)
            DeleteObject(hBackBrush);
        Cleanup();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Global variable untuk menyimpan input dari dialog
static char g_inputKeyBuffer[256] = {0};
static bool g_inputDialogResult = false;

// Dialog procedure untuk input backup key - SEDERHANA
INT_PTR CALLBACK InputKeyDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        // Center dialog dan set topmost
        {
            RECT rc;
            GetWindowRect(hwndDlg, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
            int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
            SetWindowPos(hwndDlg, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        }
        // Bring dialog to foreground and set focus
        SetForegroundWindow(hwndDlg);
        SetActiveWindow(hwndDlg);
        {
            HWND hEdit = GetDlgItem(hwndDlg, 1001);
            if (hEdit)
            {
                SetFocus(hEdit);
                SendMessage(hEdit, EM_SETSEL, 0, -1);
            }
        }
        return FALSE; // Return FALSE karena kita sudah set focus manual
        
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE)
        {
            HWND hEdit = GetDlgItem(hwndDlg, 1001);
            if (hEdit)
            {
                SetFocus(hEdit);
            }
        }
        return TRUE;
        
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            // Get text dari edit box
            GetDlgItemTextA(hwndDlg, 1001, g_inputKeyBuffer, sizeof(g_inputKeyBuffer));
            g_inputDialogResult = true;
            EndDialog(hwndDlg, IDOK);
            return TRUE;
            
        case IDCANCEL:
            g_inputKeyBuffer[0] = '\0';
            g_inputDialogResult = false;
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
        }
        break;
        
    case WM_CLOSE:
        g_inputKeyBuffer[0] = '\0';
        g_inputDialogResult = false;
        EndDialog(hwndDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// Fungsi untuk membuat dialog template secara dinamis - LARGER SIZE
HWND CreateInputDialog(HWND hParent, const char* title, const char* prompt)
{
    // Alokasi memory untuk dialog template
    WORD* p;
    WORD* pdlgtemplate;
    int nchar;
    DWORD lStyle;
    
    pdlgtemplate = p = (PWORD)LocalAlloc(LPTR, 2048);
    
    lStyle = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT;
    
    *p++ = LOWORD(lStyle);
    *p++ = HIWORD(lStyle);
    *p++ = 0; // Extended style low
    *p++ = 0; // Extended style high
    *p++ = 4; // Number of controls
    *p++ = 0; // x
    *p++ = 0; // y
    *p++ = 280; // cx - WIDER
    *p++ = 90; // cy - TALLER
    *p++ = 0; // Menu
    *p++ = 0; // Class
    
    // Title
    nchar = MultiByteToWideChar(CP_ACP, 0, title, -1, (LPWSTR)p, 128);
    p += nchar;
    
    // Font
    *p++ = 10; // Font size - LARGER
    nchar = MultiByteToWideChar(CP_ACP, 0, "Segoe UI", -1, (LPWSTR)p, 128);
    p += nchar;
    
    // Static label
    p = (WORD*)(((ULONG_PTR)p + 3) & ~3); // Align
    *p++ = LOWORD(WS_CHILD | WS_VISIBLE | SS_LEFT);
    *p++ = HIWORD(WS_CHILD | WS_VISIBLE | SS_LEFT);
    *p++ = 0; *p++ = 0; // Extended style
    *p++ = 10; *p++ = 10; // x, y
    *p++ = 260; *p++ = 16; // cx, cy - WIDER
    *p++ = 1000; // ID
    *p++ = 0xFFFF; *p++ = 0x0082; // Static class
    nchar = MultiByteToWideChar(CP_ACP, 0, prompt, -1, (LPWSTR)p, 128);
    p += nchar;
    *p++ = 0; // No creation data
    
    // Edit box - LARGER
    p = (WORD*)(((ULONG_PTR)p + 3) & ~3);
    *p++ = LOWORD(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    *p++ = HIWORD(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    *p++ = 0; *p++ = 0;
    *p++ = 10; *p++ = 30;
    *p++ = 260; *p++ = 16; // WIDER and TALLER
    *p++ = 1001; // Edit ID
    *p++ = 0xFFFF; *p++ = 0x0081; // Edit class
    *p++ = 0; // No text
    *p++ = 0;
    
    // OK Button
    p = (WORD*)(((ULONG_PTR)p + 3) & ~3);
    *p++ = LOWORD(WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP);
    *p++ = HIWORD(WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP);
    *p++ = 0; *p++ = 0;
    *p++ = 75; *p++ = 60; // Adjusted position
    *p++ = 60; *p++ = 18; // LARGER buttons
    *p++ = IDOK;
    *p++ = 0xFFFF; *p++ = 0x0080; // Button class
    nchar = MultiByteToWideChar(CP_ACP, 0, "Verify", -1, (LPWSTR)p, 128);
    p += nchar;
    *p++ = 0;
    
    // Cancel Button
    p = (WORD*)(((ULONG_PTR)p + 3) & ~3);
    *p++ = LOWORD(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP);
    *p++ = HIWORD(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP);
    *p++ = 0; *p++ = 0;
    *p++ = 145; *p++ = 60; // Adjusted position
    *p++ = 60; *p++ = 18; // LARGER buttons
    *p++ = IDCANCEL;
    *p++ = 0xFFFF; *p++ = 0x0080;
    nchar = MultiByteToWideChar(CP_ACP, 0, "Cancel", -1, (LPWSTR)p, 128);
    p += nchar;
    *p++ = 0;
    
    return (HWND)pdlgtemplate;
}

// Fungsi untuk menampilkan dialog input dan mendapatkan backup key
bool ShowInputKeyDialog(const char* title, const char* prompt, char* outputBuffer, int bufferSize)
{
    g_inputKeyBuffer[0] = '\0';
    g_inputDialogResult = false;
    
    // COMPLETELY disable all hooks before showing dialog
    HHOOK savedKbHook = g_hKeyboardHook;
    HHOOK savedMouseHook = g_hMouseHook;
    
    if (g_hKeyboardHook != NULL)
    {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = NULL;
    }
    if (g_hMouseHook != NULL)
    {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = NULL;
    }
    
    // Wait for hooks to be fully removed
    Sleep(100);
    
    // Hide main window and make it not topmost
    if (g_hMainWindow != NULL)
    {
        SetWindowPos(g_hMainWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        ShowWindow(g_hMainWindow, SW_HIDE);
    }
    
    HWND pdlgtemplate = CreateInputDialog(NULL, title, prompt);
    
    INT_PTR result = DialogBoxIndirectA(
        GetModuleHandle(NULL),
        (LPCDLGTEMPLATEA)pdlgtemplate,
        NULL,  // No parent - standalone dialog
        InputKeyDialogProc
    );
    
    LocalFree(pdlgtemplate);
    
    // Restore main window
    if (g_hMainWindow != NULL)
    {
        ShowWindow(g_hMainWindow, SW_SHOW);
        SetWindowPos(g_hMainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(g_hMainWindow);
    }
    
    // Re-enable hooks
    if (savedKbHook != NULL)
    {
        g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandle(NULL), 0);
    }
    if (savedMouseHook != NULL)
    {
        g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(NULL), 0);
    }
    
    if (result == IDOK && g_inputDialogResult && strlen(g_inputKeyBuffer) > 0)
    {
        strncpy(outputBuffer, g_inputKeyBuffer, bufferSize - 1);
        outputBuffer[bufferSize - 1] = '\0';
        return true;
    }
    
    return false;
}

// Fungsi untuk menampilkan first time login dialog dengan input manual
bool ShowFirstTimeLoginDialog()
{
    // Pastikan keys sudah di-load
    if (g_backupKeys.empty())
    {
        if (!LoadAllBackupKeys())
        {
            MessageBoxA(NULL, "Failed to load backup keys!\nPlease restart the application.", 
                       "Error", MB_OK | MB_ICONERROR);
            return false;
        }
    }
    
    bool verified = false;
    int attempts = 0;
    const int maxAttempts = 10;
    
    while (!verified && attempts < maxAttempts)
    {
        attempts++;
        
        char inputKey[256] = {0};
        
        std::stringstream promptMsg;
        promptMsg << "Enter backup key (Attempt " << attempts << "/" << maxAttempts << "):";
        
        if (ShowInputKeyDialog("Initial Verification", promptMsg.str().c_str(), inputKey, sizeof(inputKey)))
        {
            std::string keyStr = TrimString(inputKey);
            int keyIndex = VerifyBackupKey(keyStr);
            
            if (keyIndex >= 0)
            {
                if (IsKeyUsed(keyIndex))
                {
                    std::stringstream errMsg;
                    errMsg << "Key #" << (keyIndex + 1) << " has already been used!\n";
                    errMsg << "Please use a different backup key.";
                    MessageBoxA(NULL, errMsg.str().c_str(), "Key Already Used", MB_OK | MB_ICONWARNING | MB_TOPMOST);
                }
                else
                {
                    // Key valid - mark as used
                    g_verifiedKeyIndex = keyIndex;
                    MarkKeyAsUsed(keyIndex);
                    
                    std::stringstream successMsg;
                    successMsg << "Key #" << (keyIndex + 1) << " verified successfully!\n\n";
                    successMsg << "This key has now been marked as USED.\n";
                    successMsg << "Remaining keys: " << GetAvailableKeyCount() << " of " << TOTAL_BACKUP_KEYS;
                    
                    MessageBoxA(NULL, successMsg.str().c_str(), "Verification Successful", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
                    verified = true;
                }
            }
            else
            {
                std::stringstream errMsg;
                errMsg << "Invalid backup key!\n\n";
                errMsg << "Please check your key and try again.\n";
                errMsg << "Remaining attempts: " << (maxAttempts - attempts);
                MessageBoxA(NULL, errMsg.str().c_str(), "Invalid Key", MB_OK | MB_ICONERROR | MB_TOPMOST);
            }
        }
        else
        {
            // User cancelled
            MessageBoxA(NULL, 
                       "Verification is required to continue.\n"
                       "You cannot skip this step.",
                       "Cannot Skip", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        }
    }
    
    if (!verified)
    {
        MessageBoxA(NULL,
                   "Maximum verification attempts exceeded.\n"
                   "Application will exit.\n\n"
                   "Please restart and try again.",
                   "Verification Failed", MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
    
    return verified;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    if (!ElevateProcess())
    {
        return 1;
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    // Check apakah first time setup diperlukan
    if (IsFirstTimeSetup())
    {
        g_firstTimeSetupMode = true;
        
        MessageBoxA(NULL,
                   "Welcome to Task-Based Authentication!\n\n"
                   "This is your FIRST TIME running this application.\n\n"
                   "You will now be given 10 BACKUP KEYS.\n"
                   "IMPORTANT: Take a photo or write down these keys!\n"
                   "They will NEVER be shown again after this setup.\n\n"
                   "Click OK to generate your backup keys.",
                   "First Time Setup", MB_OK | MB_ICONINFORMATION);
        
        if (!ShowFirstTimeSetupDialog())
        {
            MessageBoxA(NULL, "Setup failed. Application will exit.", 
                       "Error", MB_OK | MB_ICONERROR);
            return 1;
        }
        
        // Setelah setup, minta user login dengan salah satu backup key
        MessageBoxA(NULL,
                   "Setup complete!\n\n"
                   "For your FIRST LOGIN, you need to enter ONE of your backup keys.\n"
                   "This is to verify that you have saved the keys correctly.\n\n"
                   "After this initial verification, you can login using the file selection method.",
                   "Initial Verification Required", MB_OK | MB_ICONINFORMATION);
        
        // Show first time login dialog
        if (!ShowFirstTimeLoginDialog())
        {
            MessageBoxA(NULL, "Initial verification failed. Application will exit.", 
                       "Error", MB_OK | MB_ICONERROR);
            return 1;
        }
        
        g_firstTimeSetupMode = false;
        
        MessageBoxA(NULL,
                   "✓ Initial verification successful!\n\n"
                   "From now on, you can login by:\n"
                   "1. Selecting the 3 files you worked on recently\n"
                   "2. Using a backup key if you forget the files\n\n"
                   "Remember: Each backup key can only be used ONCE!",
                   "Setup Complete", MB_OK | MB_ICONINFORMATION);
        
        // Exit after first time setup - user harus run lagi untuk normal login
        return 0;
    }
    
    // Load existing backup keys
    if (!LoadAllBackupKeys())
    {
        MessageBoxA(NULL, 
                   "Error loading backup keys.\n"
                   "The backup key file may be corrupted.\n\n"
                   "Please delete backup_key.dat and restart the application.",
                   "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_recentFiles = LoadFilesFromJson();
    g_correctFiles = GetRandomCorrectFiles(g_recentFiles, 3);
    g_challengeFiles = GenerateChallengeFiles(g_correctFiles);

    DisableTaskManager();

    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc,
                                       hInstance, 0);
    g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc,
                                    hInstance, 0);

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    wc.lpszClassName = "AuthAppClass";

    RegisterClassExA(&wc);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    g_hMainWindow = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "AuthAppClass",
        "Task-Based Authentication",
        WS_POPUP | WS_VISIBLE,
        0, 0, screenWidth, screenHeight,
        NULL, NULL, hInstance, NULL);

    if (!g_hMainWindow)
    {
        Cleanup();
        return 1;
    }

    ShowWindow(g_hMainWindow, SW_MAXIMIZE);
    UpdateWindow(g_hMainWindow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

