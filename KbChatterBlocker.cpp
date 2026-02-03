#include <windows.h>
#include <unordered_map>
#include <chrono>

// Configuration
const int CHATTER_THRESHOLD_MS = 40;         // Block everything faster than this
const int REPEAT_TRANSITION_DELAY_MS = 150;  // Time to enter repeat mode

int REPEAT_THRESHOLD_MS = 0;  // Will be set from system settings on startup

struct KeyState {
    long long lastPressTime = 0;
    bool inRepeatMode = false;
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;

long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

void InitializeSystemKeyboardSettings() {
    // Get keyboard repeat rate from Windows
    // KeyboardSpeed ranges from 0 (slow, ~2.5 reps/sec) to 31 (fast, ~30 reps/sec)
    int keyboardSpeed = 0;
    SystemParametersInfo(SPI_GETKEYBOARDSPEED, 0, &keyboardSpeed, 0);
    
    // Convert to milliseconds between repeats
    // Formula: approximately 1000ms / (2.5 + speed * 0.88) 
    // Speed 0 = ~400ms, Speed 31 = ~33ms
    float repsPerSecond = 2.5f + (keyboardSpeed * 0.88f);
    int repeatRateMs = (int)(1000.0f / repsPerSecond);
    
    // Use HALF of the system repeat rate to ensure we don't block legitimate repeats
    // This gives us headroom for timing variations
    REPEAT_THRESHOLD_MS = repeatRateMs / 2;
    
    // Ensure minimum of 10ms
    if (REPEAT_THRESHOLD_MS < 10) {
        REPEAT_THRESHOLD_MS = 10;
    }
}

bool ShouldBlockKey(DWORD vkCode) {
    KeyState& state = keyStates[vkCode];
    long long currentTime = GetCurrentTimeMs();

    if (state.lastPressTime == 0) {
        state.lastPressTime = currentTime;
        return false;
    }

    long long timeSincePress = currentTime - state.lastPressTime;

    // Check if we should enter repeat mode
    if (timeSincePress > REPEAT_TRANSITION_DELAY_MS) {
        state.inRepeatMode = true;
    }

    // Use different threshold for repeat mode
    int threshold = state.inRepeatMode ? REPEAT_THRESHOLD_MS : CHATTER_THRESHOLD_MS;

    // Block if faster than threshold
    if (timeSincePress < threshold) {
        return true;
    }

    state.lastPressTime = currentTime;
    return false;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKbdStruct->vkCode;

        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        
        // Reset repeat mode on key release
        if (isKeyUp) {
            keyStates[vkCode].inRepeatMode = false;
        }
        
        if (isKeyDown && ShouldBlockKey(vkCode)) {
            return 1; // Block the key
        }
    }

    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize keyboard settings from system
    InitializeSystemKeyboardSettings();
    
    // Create mutex to prevent multiple instances
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"KbChatterBlockerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    // Install keyboard hook
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    
    if (hHook == NULL) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 1;
    }

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    UnhookWindowsHookEx(hHook);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    return 0;
}
