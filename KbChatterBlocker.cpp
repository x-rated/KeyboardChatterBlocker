#include <windows.h>
#include <unordered_map>
#include <chrono>

// Configuration
const int CHATTER_THRESHOLD_MS = 30;  // Block everything faster than this

struct KeyState {
    long long lastPressTime = 0;
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;

long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool ShouldBlockKey(DWORD vkCode) {
    KeyState& state = keyStates[vkCode];
    long long currentTime = GetCurrentTimeMs();

    if (state.lastPressTime == 0) {
        state.lastPressTime = currentTime;
        return false;
    }

    long long timeSincePress = currentTime - state.lastPressTime;

    // Block if faster than threshold
    if (timeSincePress < CHATTER_THRESHOLD_MS) {
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

        if (isKeyDown && ShouldBlockKey(vkCode)) {
            return 1; // Block the key
        }
    }

    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
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
