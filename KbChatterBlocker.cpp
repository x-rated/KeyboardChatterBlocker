#include <windows.h>
#include <iostream>
#include <unordered_map>
#include <chrono>

// Configuration
const int INITIAL_CHATTER_THRESHOLD_MS = 50;  // Strict threshold for first repeat
const int REPEAT_CHATTER_THRESHOLD_MS = 15;   // Lenient threshold for subsequent repeats
const int REPEAT_TRANSITION_DELAY_MS = 200;   // Time to switch to repeat mode

struct KeyState {
    long long lastPressTime = 0;
    long long lastReleaseTime = 0;
    bool inRepeatMode = false;
    int blockedCount = 0;
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;

long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool ShouldBlockKey(DWORD vkCode, bool isKeyDown) {
    KeyState& state = keyStates[vkCode];
    long long currentTime = GetCurrentTimeMs();

    if (isKeyDown) {
        // Handle key press
        if (state.lastPressTime == 0) {
            // First press ever - always allow
            state.lastPressTime = currentTime;
            return false;
        }

        long long timeSincePress = currentTime - state.lastPressTime;

        // Determine which threshold to use
        int threshold;
        if (state.inRepeatMode) {
            threshold = REPEAT_CHATTER_THRESHOLD_MS;
        } else {
            threshold = INITIAL_CHATTER_THRESHOLD_MS;
            
            // Check if we should enter repeat mode
            if (timeSincePress > REPEAT_TRANSITION_DELAY_MS) {
                state.inRepeatMode = true;
            }
        }

        // Block if within threshold
        if (timeSincePress < threshold) {
            state.blockedCount++;
            std::cout << "[BLOCKED] VK:" << vkCode 
                      << " - " << timeSincePress << "ms since last press"
                      << " (threshold: " << threshold << "ms)"
                      << " - Total blocked: " << state.blockedCount << std::endl;
            return true;
        }

        // Update press time and allow
        state.lastPressTime = currentTime;
        return false;
    } else {
        // Handle key release
        state.lastReleaseTime = currentTime;
        state.inRepeatMode = false;
        return false;
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKbdStruct->vkCode;

        // Allow ESC to exit
        if (vkCode == VK_ESCAPE && wParam == WM_KEYDOWN) {
            std::cout << "\n[INFO] ESC pressed - Exiting..." << std::endl;
            PostQuitMessage(0);
            return CallNextHookEx(hHook, nCode, wParam, lParam);
        }

        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        
        if (ShouldBlockKey(vkCode, isKeyDown)) {
            return 1; // Block the key
        }
    }

    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "Adaptive Keyboard Chatter Blocker" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "Initial chatter threshold: " << INITIAL_CHATTER_THRESHOLD_MS << "ms" << std::endl;
    std::cout << "Repeat chatter threshold: " << REPEAT_CHATTER_THRESHOLD_MS << "ms" << std::endl;
    std::cout << "Repeat mode delay: " << REPEAT_TRANSITION_DELAY_MS << "ms" << std::endl;
    std::cout << "\nPress ESC to exit" << std::endl;
    std::cout << "============================================================\n" << std::endl;

    // Install keyboard hook
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    
    if (hHook == NULL) {
        std::cerr << "Failed to install hook! Error: " << GetLastError() << std::endl;
        return 1;
    }

    std::cout << "[INFO] Hook installed successfully. Monitoring keyboard..." << std::endl;

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    UnhookWindowsHookEx(hHook);
    std::cout << "\n[INFO] Hook removed. Exiting..." << std::endl;

    return 0;
}
