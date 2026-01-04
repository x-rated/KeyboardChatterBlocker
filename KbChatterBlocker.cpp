#include <windows.h>
#include <unordered_map>
#include <chrono>
#include <string>
#include <fstream>
#include <iomanip>
#include <sstream>

// Configuration
const int INITIAL_CHATTER_THRESHOLD_MS = 100;
const int REPEAT_CHATTER_THRESHOLD_MS = 15;
const int REPEAT_TRANSITION_DELAY_MS = 200;
const int MIN_RELEASE_DURATION_MS = 25;       // Minimum time key must be released for intentional tap
const int MIN_HELD_DURATION_MS = 30;          // Minimum time key must be held for intentional tap

struct KeyState {
    long long lastPressTime = 0;
    long long lastReleaseTime = 0;
    bool inRepeatMode = false;
    int blockedCount = 0;
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;
HWND hStatusWindow = NULL;
HWND hStatusText = NULL;
int totalBlocked = 0;
std::ofstream logFile;

void LogToFile(const std::wstring& message) {
    if (!logFile.is_open()) {
        logFile.open("C:\\KbChatterBlocker_log.txt", std::ios::app);
    }
    
    if (logFile.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        
        char timeStr[64];
        sprintf_s(timeStr, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        
        logFile << timeStr;
        
        // Convert wstring to string for logging
        std::string str(message.begin(), message.end());
        logFile << str << std::endl;
        logFile.flush();
    }
}

long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

void UpdateStatus(const std::wstring& message) {
    if (hStatusText) {
        SetWindowText(hStatusText, message.c_str());
    }
    LogToFile(message);
}

bool ShouldBlockKey(DWORD vkCode, bool isKeyDown) {
    KeyState& state = keyStates[vkCode];
    long long currentTime = GetCurrentTimeMs();

    if (isKeyDown) {
        if (state.lastPressTime == 0) {
            state.lastPressTime = currentTime;
            UpdateStatus(L"First press of VK" + std::to_wstring(vkCode));
            return false;
        }

        long long timeSincePress = currentTime - state.lastPressTime;
        long long timeSinceRelease = currentTime - state.lastReleaseTime;
        long long keyHeldDuration = state.lastReleaseTime - state.lastPressTime;

        // STRATEGY: Distinguish between chatter and intentional double-tap
        // 
        // Chatter characteristics:
        //   - Very short hold duration (key barely pressed, like 5-15ms)
        //   - Very short or no gap between release and next press
        //   - Erratic, unintentional bouncing
        //
        // Intentional double-tap characteristics:
        //   - Reasonable hold duration (finger actually pressed, 30-80ms)
        //   - Clear gap after release (finger lifted and pressed again, 25-50ms)
        //   - Deliberate rhythm

        bool wasProperlyReleased = state.lastReleaseTime > state.lastPressTime;
        bool wasHeldLongEnough = keyHeldDuration >= MIN_HELD_DURATION_MS;
        bool hasProperGap = timeSinceRelease >= MIN_RELEASE_DURATION_MS;
        
        // If this looks like an intentional double-tap, allow it regardless of timing
        if (wasProperlyReleased && wasHeldLongEnough && hasProperGap) {
            state.lastPressTime = currentTime;
            state.inRepeatMode = false;
            std::wstring status = L"✓ Intentional double-tap VK" + std::to_wstring(vkCode) + 
                                 L" | P→P:" + std::to_wstring(timeSincePress) + 
                                 L"ms | Held:" + std::to_wstring(keyHeldDuration) + 
                                 L"ms | Gap:" + std::to_wstring(timeSinceRelease) + L"ms";
            UpdateStatus(status);
            return false;
        }

        // Check if we're in repeat/hold mode (holding a key down)
        int threshold;
        if (state.inRepeatMode) {
            threshold = REPEAT_CHATTER_THRESHOLD_MS;
        } else {
            threshold = INITIAL_CHATTER_THRESHOLD_MS;
            if (timeSincePress > REPEAT_TRANSITION_DELAY_MS) {
                state.inRepeatMode = true;
            }
        }

        // Block if within threshold and doesn't look like intentional input
        if (timeSincePress < threshold) {
            state.blockedCount++;
            totalBlocked++;
            
            std::wstring reason;
            if (!wasProperlyReleased) {
                reason = L"no-release";
            } else if (!wasHeldLongEnough) {
                reason = L"held:" + std::to_wstring(keyHeldDuration) + L"ms<" + std::to_wstring(MIN_HELD_DURATION_MS);
            } else if (!hasProperGap) {
                reason = L"gap:" + std::to_wstring(timeSinceRelease) + L"ms<" + std::to_wstring(MIN_RELEASE_DURATION_MS);
            } else {
                reason = L"under-threshold";
            }
            
            std::wstring status = L"✗ BLOCKED #" + std::to_wstring(totalBlocked) + 
                                 L" VK" + std::to_wstring(vkCode) + 
                                 L" | P→P:" + std::to_wstring(timeSincePress) + 
                                 L"ms | " + reason;
            UpdateStatus(status);
            return true;
        }

        state.lastPressTime = currentTime;
        std::wstring status = L"Normal press VK" + std::to_wstring(vkCode) + 
                             L" (" + std::to_wstring(timeSincePress) + L"ms)";
        UpdateStatus(status);
        return false;
    } else {
        state.lastReleaseTime = currentTime;
        state.inRepeatMode = false;
        return false;
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKbdStruct->vkCode;

        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        
        if (ShouldBlockKey(vkCode, isKeyDown)) {
            return 1;
        }
    }

    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        
        case WM_COMMAND:
            if (LOWORD(wParam) == 1) { // Close button
                DestroyWindow(hwnd);
            }
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Register window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"KbChatterBlockerClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    
    if (!RegisterClass(&wc)) {
        MessageBox(NULL, L"Failed to register window class!", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create window
    hStatusWindow = CreateWindowEx(
        0,
        L"KbChatterBlockerClass",
        L"Keyboard Chatter Blocker - Diagnostic Mode",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 250,
        NULL, NULL, hInstance, NULL
    );

    if (!hStatusWindow) {
        MessageBox(NULL, L"Failed to create window!", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create status text
    CreateWindow(L"STATIC", L"Configuration:",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, 10, 470, 20,
        hStatusWindow, NULL, hInstance, NULL);

    std::wstring config = L"Initial Threshold: " + std::to_wstring(INITIAL_CHATTER_THRESHOLD_MS) + 
                         L"ms | Repeat: " + std::to_wstring(REPEAT_CHATTER_THRESHOLD_MS) + 
                         L"ms | Min Release: " + std::to_wstring(MIN_RELEASE_DURATION_MS) + L"ms";
    CreateWindow(L"STATIC", config.c_str(),
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, 35, 470, 20,
        hStatusWindow, NULL, hInstance, NULL);

    CreateWindow(L"STATIC", L"_____________________________________________",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, 55, 470, 20,
        hStatusWindow, NULL, hInstance, NULL);

    hStatusText = CreateWindow(L"STATIC", L"Status: Starting...",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, 80, 470, 40,
        hStatusWindow, NULL, hInstance, NULL);

    CreateWindow(L"STATIC", L"The app is running and monitoring your keyboard.\nBlocked chatter events will be shown above.",
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        10, 130, 470, 40,
        hStatusWindow, NULL, hInstance, NULL);

    // Create close button
    CreateWindow(L"BUTTON", L"Close && Exit",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        200, 180, 100, 25,
        hStatusWindow, (HMENU)1, hInstance, NULL);

    ShowWindow(hStatusWindow, nCmdShow);
    UpdateWindow(hStatusWindow);

    // Initialize log file
    logFile.open("C:\\KbChatterBlocker_log.txt", std::ios::trunc);
    if (logFile.is_open()) {
        logFile << "=== Keyboard Chatter Blocker Log ===" << std::endl;
        logFile << "Initial Threshold: " << INITIAL_CHATTER_THRESHOLD_MS << "ms" << std::endl;
        logFile << "Repeat Threshold: " << REPEAT_CHATTER_THRESHOLD_MS << "ms" << std::endl;
        logFile << "Min Held Duration: " << MIN_HELD_DURATION_MS << "ms" << std::endl;
        logFile << "Min Release Duration: " << MIN_RELEASE_DURATION_MS << "ms" << std::endl;
        logFile << "=====================================" << std::endl << std::endl;
        logFile.flush();
    }

    // Install keyboard hook
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    
    if (hHook == NULL) {
        DWORD error = GetLastError();
        std::wstring errorMsg = L"Failed to install keyboard hook!\nError code: " + std::to_wstring(error);
        MessageBox(hStatusWindow, errorMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
        UpdateStatus(L"Status: FAILED - Hook not installed!");
    } else {
        UpdateStatus(L"Status: Running | Blocked: 0 | Waiting for chatter...");
    }

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    if (hHook) {
        UnhookWindowsHookEx(hHook);
    }
    
    if (logFile.is_open()) {
        logFile << std::endl << "=== Session ended ===" << std::endl;
        logFile.close();
    }

    return 0;
}
