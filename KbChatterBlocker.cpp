#include <windows.h>
#include <unordered_map>

constexpr DWORD CHATTER_THRESHOLD_MS = 100;

HHOOK g_hook = nullptr;
std::unordered_map<DWORD, DWORD> lastPressTime;

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN)
    {
        KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // ignoruj opakované stisky při držení klávesy (auto-repeat)
        if (kb->flags & LLKHF_REPEAT)
            return CallNextHookEx(g_hook, nCode, wParam, lParam);

        DWORD key = kb->vkCode;
        DWORD now = GetTickCount();

        auto it = lastPressTime.find(key);
        if (it != lastPressTime.end())
        {
            if (now - it->second < CHATTER_THRESHOLD_MS)
                return 1; // blokuj chatter
        }

        lastPressTime[key] = now;
    }

    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hook = SetWindowsHookEx(
        WH_KEYBOARD_LL,
        KeyboardProc,
        hInst,
        0
    );

    if (!g_hook)
        return 1;

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(g_hook);
    return 0;
}
