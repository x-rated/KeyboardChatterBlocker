#include <windows.h>
#include <unordered_map>
#include <chrono>

// Configuration
const int CHATTER_THRESHOLD_MS = 50;         // Block everything faster than this
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
    int keyboardSpeed = 0;
    SystemParametersInfo(SPI_GETKEYBOARDSPEED, 0, &keyboardSpeed, 0);
    
    float repsPerSecond = 2.5f + (keyboardSpeed * 0.88f);
    int repeatRateMs = (int)(1000.0f / repsPerSecond);
    
    REPEAT_THRESHOLD_MS = repeatRateMs / 2;
    
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

    if (timeSincePress > REPEAT_TRANSITION_DELAY_MS) {
        state.inRepeatMode = true;
    }

    int threshold = state.inRepeatMode ? REPEAT_THRESHOLD_MS : CHATTER_THRESHOLD_MS;

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
        bool isKeyUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

        if (isKeyUp) {
            keyStates[vkCode].inRepeatMode = false;
        }

        if (isKeyDown && ShouldBlockKey(vkCode)) {
            return 1;
        }
    }

    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    InitializeSystemKeyboardSettings();

    // Create mutex to prevent multiple instances
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"KbChatterBlockerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    // Register a window class and create a hidden message window
    // (makes the process identifiable and anchors the message loop to a real window)
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"KbChatterBlocker";
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(
        0, L"KbChatterBlocker", L"Keyboard Chatter Blocker",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL
    );
    // Window stays hidden — we never call ShowWindow

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
