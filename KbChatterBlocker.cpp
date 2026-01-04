#include <windows.h>
#include <unordered_map>

constexpr double CHATTER_THRESHOLD_MS = 100.0;

struct KeyTime {
    LARGE_INTEGER lastPress;
};

HINSTANCE g_hInst = nullptr;
std::unordered_map<USHORT, KeyTime> lastPressTime;
LARGE_INTEGER freq;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INPUT)
    {
        UINT dwSize = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize == 0) return 0;

        RAWINPUT* raw = (RAWINPUT*)malloc(dwSize);
        if (!raw) return 0;

        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, raw, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize)
        {
            if (raw->header.dwType == RIM_TYPEKEYBOARD)
            {
                USHORT key = raw->data.keyboard.VKey;
                USHORT flags = raw->data.keyboard.Flags;

                // ignoruj uvolnění klávesy
                if (!(flags & RI_KEY_BREAK))
                {
                    LARGE_INTEGER now;
                    QueryPerformanceCounter(&now);

                    auto it = lastPressTime.find(key);
                    if (it != lastPressTime.end())
                    {
                        double elapsedMs = (double)(now.QuadPart - it->second.lastPress.QuadPart) * 1000.0 / (double)freq.QuadPart;
                        if (elapsedMs < CHATTER_THRESHOLD_MS)
                        {
                            free(raw);
                            return 0; // blokuj chatter
                        }
                    }

                    lastPressTime[key].lastPress = now;
                }
            }
        }

        free(raw);
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;
    QueryPerformanceFrequency(&freq);

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"ChatterBlockerWindow";
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, wc.lpszClassName, L"ChatterBlocker", 0,
                               0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);

    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01; // Generic desktop controls
    rid.usUsage = 0x06;     // Keyboard
    rid.dwFlags = RIDEV_NOLEGACY; // ignoruj WM_KEYDOWN
    rid.hwndTarget = hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
