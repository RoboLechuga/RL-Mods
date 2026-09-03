#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "ScreenshotControl.hpp"

#pragma comment(lib, "Gdi32.lib")

namespace ScreenshotControl
{
    namespace
    {
        constexpr std::uintptr_t CURRENT_MENU_RELOCATED =
            0x00F6062C;
        constexpr std::uintptr_t CURRENT_MENU_LEGACY =
            0x0135F62C;

        constexpr int DEFAULT_DELAY_MS = 10000;
        constexpr int MIN_DELAY_MS = 3000;
        constexpr int MAX_DELAY_MS = 20000;
        constexpr int DELAY_STEP_MS = 1000;

        constexpr const wchar_t* INI_SECTION =
            L"Screenshot";
        constexpr const wchar_t* INI_KEY_ENABLED =
            L"Enabled";
        constexpr const wchar_t* INI_KEY_DELAY =
            L"DelayMs";
        constexpr const wchar_t* INI_KEY_DEBUG =
            L"Debug";

        constexpr const char* VERSION_TEXT =
            "RL-Mods 1.3";

        bool g_enabled = true;
        bool g_debug = false;
        int g_delayMs = DEFAULT_DELAY_MS;

        bool g_captured = false;
        ULONGLONG g_captureAt = 0;
        std::string g_scoreMenu;

        std::string g_lastMenu;
        std::string g_status;
        bool g_showHelp = false;

        HWND g_overlay = nullptr;
        HFONT g_font = nullptr;
        ULONGLONG g_hideAt = 0;

        std::wstring g_iniPath;

        bool IsRocksmithForeground()
        {
            HWND foreground =
                GetForegroundWindow();

            if (!foreground)
                return false;

            DWORD processId = 0;

            GetWindowThreadProcessId(
                foreground,
                &processId);

            return
                processId ==
                GetCurrentProcessId();
        }

        bool KeyPressed(
            int virtualKey)
        {
            const SHORT state =
                GetAsyncKeyState(
                    virtualKey);

            if ((state & 1) == 0)
                return false;

            return IsRocksmithForeground();
        }

        bool IsReadableAddress(
            const void* address)
        {
            if (!address)
                return false;

            MEMORY_BASIC_INFORMATION mbi{};

            if (!VirtualQuery(
                    address,
                    &mbi,
                    sizeof(mbi)))
            {
                return false;
            }

            if (mbi.State != MEM_COMMIT)
                return false;

            if (mbi.Protect & PAGE_GUARD)
                return false;

            if (mbi.Protect & PAGE_NOACCESS)
                return false;

            const DWORD readable =
                PAGE_READONLY |
                PAGE_READWRITE |
                PAGE_WRITECOPY |
                PAGE_EXECUTE_READ |
                PAGE_EXECUTE_READWRITE |
                PAGE_EXECUTE_WRITECOPY;

            return
                (mbi.Protect &
                    readable) != 0;
        }

        std::uintptr_t DereferenceAndAdd(
            std::uintptr_t address,
            std::uintptr_t offset)
        {
            if (!IsReadableAddress(
                    reinterpret_cast<
                        const void*>(
                            address)))
            {
                return 0;
            }

            const std::uintptr_t next =
                *reinterpret_cast<
                    const std::uintptr_t*>(
                        address);

            if (!next)
                return 0;

            return next + offset;
        }

        std::uintptr_t ResolveMenuFromBase(
            std::uintptr_t address)
        {
            address =
                DereferenceAndAdd(
                    address,
                    0x28);

            if (!address)
                return 0;

            address =
                DereferenceAndAdd(
                    address,
                    0x8C);

            if (!address)
                return 0;

            return
                DereferenceAndAdd(
                    address,
                    0x0);
        }

        std::string ReadMenuString(
            std::uintptr_t address)
        {
            if (!address)
                return {};

            const char* text =
                reinterpret_cast<
                    const char*>(
                        address);

            if (!IsReadableAddress(text))
                return {};

            constexpr size_t MAX_MENU_LENGTH =
                128;

            size_t length = 0;

            while (length <
                MAX_MENU_LENGTH)
            {
                const char* current =
                    text + length;

                if (!IsReadableAddress(
                        current))
                {
                    return {};
                }

                const unsigned char c =
                    static_cast<unsigned char>(
                        *current);

                if (c == '\0')
                    break;

                if (c < 32 ||
                    c > 126)
                {
                    return {};
                }

                ++length;
            }

            if (length == 0 ||
                length ==
                    MAX_MENU_LENGTH)
            {
                return {};
            }

            return
                std::string(
                    text,
                    length);
        }

        std::string CurrentMenu()
        {
            HMODULE gameModule =
                GetModuleHandleW(nullptr);

            if (!gameModule)
                return {};

            const std::uintptr_t moduleBase =
                reinterpret_cast<
                    std::uintptr_t>(
                        gameModule);

            std::string menu =
                ReadMenuString(
                    ResolveMenuFromBase(
                        moduleBase +
                        CURRENT_MENU_RELOCATED));

            if (!menu.empty())
                return menu;

            return
                ReadMenuString(
                    ResolveMenuFromBase(
                        CURRENT_MENU_LEGACY));
        }

        bool IsScoreMenu(
            const std::string& menu)
        {
            return
                menu.find(
                    "LearnASong_SongReview") !=
                    std::string::npos ||
                menu.find(
                    "ScoreAttack_SongReview") !=
                    std::string::npos ||
                menu.find(
                    "Duet_SongReview") !=
                    std::string::npos ||
                menu.find(
                    "H2H_SongReview") !=
                    std::string::npos;
        }

        void TakeScreenshot()
        {
            INPUT inputs[2]{};

            inputs[0].type =
                INPUT_KEYBOARD;
            inputs[0].ki.wVk =
                VK_F12;

            inputs[1].type =
                INPUT_KEYBOARD;
            inputs[1].ki.wVk =
                VK_F12;
            inputs[1].ki.dwFlags =
                KEYEVENTF_KEYUP;

            SendInput(
                2,
                inputs,
                sizeof(INPUT));
        }

        void ResetCapture()
        {
            g_scoreMenu.clear();
            g_captureAt = 0;
            g_captured = false;
        }

        void BuildIniPath()
        {
            wchar_t path[MAX_PATH] = {};

            const DWORD length =
                GetModuleFileNameW(
                    nullptr,
                    path,
                    MAX_PATH);

            if (length == 0 ||
                length >= MAX_PATH)
            {
                g_iniPath =
                    L"RLMods.ini";
                return;
            }

            std::wstring fullPath(path);

            const size_t slash =
                fullPath.find_last_of(
                    L"\\/");

            if (slash ==
                std::wstring::npos)
            {
                g_iniPath =
                    L"RLMods.ini";
                return;
            }

            g_iniPath =
                fullPath.substr(
                    0,
                    slash + 1) +
                L"RLMods.ini";
        }

        bool ReadIniBool(
            const wchar_t* key,
            bool defaultValue)
        {
            return
                GetPrivateProfileIntW(
                    INI_SECTION,
                    key,
                    defaultValue
                        ? 1
                        : 0,
                    g_iniPath.c_str()) != 0;
        }

        int ReadIniInt(
            const wchar_t* key,
            int defaultValue)
        {
            return
                static_cast<int>(
                    GetPrivateProfileIntW(
                        INI_SECTION,
                        key,
                        defaultValue,
                        g_iniPath.c_str()));
        }

        void WriteIniInt(
            const wchar_t* key,
            int value)
        {
            wchar_t buffer[32] = {};

            swprintf_s(
                buffer,
                L"%d",
                value);

            WritePrivateProfileStringW(
                INI_SECTION,
                key,
                buffer,
                g_iniPath.c_str());
        }

        void SaveSettings()
        {
            WriteIniInt(
                INI_KEY_ENABLED,
                g_enabled ? 1 : 0);

            WriteIniInt(
                INI_KEY_DELAY,
                g_delayMs);

            WriteIniInt(
                INI_KEY_DEBUG,
                g_debug ? 1 : 0);
        }

        std::string OverlayText()
        {
            char buffer[1024] = {};

            if (g_showHelp)
            {
                sprintf_s(
                    buffer,
                    "RL-Mods Hotkeys\n"
                    "\n"
                    ", / .   Drop tuning down / up\n"
                    "; / '   Reference pitch down / up\n"
                    "\\      Reset reference to A440\n"
                    "F4      Show this hotkey help\n"
                    "F5      Auto Screenshot ON / OFF\n"
                    "F6/F7   Screenshot delay -1s / +1s\n"
                    "F8      Refresh song enumeration\n"
                    "F9      Tuning mode: P1 / P2 / Sync / Auto\n"
                    "\n"
                    "\"If it sounds bad, that's probably still you.\"\n"
                    "%s",
                    VERSION_TEXT);
            }
            else if (g_debug)
            {
                const char* menuText =
                    g_lastMenu.empty()
                    ? "<unresolved>"
                    : g_lastMenu.c_str();

                sprintf_s(
                    buffer,
                    "Auto Screenshot: %s    Delay: %.1fs\n"
                    "Menu: %s\n"
                    "Status: %s",
                    g_enabled
                        ? "ON"
                        : "OFF",
                    g_delayMs /
                        1000.0,
                    menuText,
                    g_status.c_str());
            }
            else
            {
                sprintf_s(
                    buffer,
                    "Auto Screenshot: %s    Delay: %.1fs",
                    g_enabled
                        ? "ON"
                        : "OFF",
                    g_delayMs /
                        1000.0);
            }

            return buffer;
        }

        int OverlayWidth()
        {
            if (g_showHelp)
                return 620;

            if (g_debug)
                return 720;

            return 470;
        }

        int OverlayHeight()
        {
            if (g_showHelp)
                return 300;

            if (g_debug)
                return 118;

            return 64;
        }

        LRESULT CALLBACK OverlayProc(
            HWND hwnd,
            UINT message,
            WPARAM wParam,
            LPARAM lParam)
        {
            switch (message)
            {
            case WM_ERASEBKGND:
                return 1;

            case WM_PAINT:
            {
                PAINTSTRUCT ps{};

                HDC dc =
                    BeginPaint(
                        hwnd,
                        &ps);

                RECT rect{};

                GetClientRect(
                    hwnd,
                    &rect);

                HBRUSH background =
                    CreateSolidBrush(
                        RGB(20, 20, 20));

                FillRect(
                    dc,
                    &rect,
                    background);

                DeleteObject(background);

                SetBkMode(
                    dc,
                    TRANSPARENT);

                SetTextColor(
                    dc,
                    RGB(245, 245, 245));

                HFONT previousFont =
                    nullptr;

                if (g_font)
                {
                    previousFont =
                        reinterpret_cast<HFONT>(
                            SelectObject(
                                dc,
                                g_font));
                }

                RECT textRect = rect;
                textRect.left += 18;
                textRect.right -= 18;
                textRect.top += 10;
                textRect.bottom -= 10;

                const std::string text =
                    OverlayText();

                DrawTextA(
                    dc,
                    text.c_str(),
                    -1,
                    &textRect,
                    DT_LEFT |
                    DT_TOP |
                    DT_WORDBREAK |
                    DT_NOPREFIX);

                if (previousFont)
                {
                    SelectObject(
                        dc,
                        previousFont);
                }

                EndPaint(
                    hwnd,
                    &ps);

                return 0;
            }

            case WM_NCHITTEST:
                return HTTRANSPARENT;

            default:
                return
                    DefWindowProc(
                        hwnd,
                        message,
                        wParam,
                        lParam);
            }
        }

        bool EnsureOverlay()
        {
            if (g_overlay)
                return true;

            HINSTANCE instance =
                GetModuleHandleW(nullptr);

            const wchar_t* className =
                L"RLModsScreenshotOSD";

            WNDCLASSW wc{};
            wc.lpfnWndProc =
                OverlayProc;
            wc.hInstance =
                instance;
            wc.lpszClassName =
                className;
            wc.hCursor =
                LoadCursor(
                    nullptr,
                    IDC_ARROW);

            RegisterClassW(&wc);

            g_font =
                CreateFontW(
                    -20,
                    0,
                    0,
                    0,
                    FW_SEMIBOLD,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    DEFAULT_PITCH |
                        FF_DONTCARE,
                    L"Segoe UI");

            g_overlay =
                CreateWindowExW(
                    WS_EX_TOPMOST |
                    WS_EX_TOOLWINDOW |
                    WS_EX_LAYERED |
                    WS_EX_TRANSPARENT |
                    WS_EX_NOACTIVATE,
                    className,
                    L"",
                    WS_POPUP,
                    40,
                    118,
                    OverlayWidth(),
                    OverlayHeight(),
                    nullptr,
                    nullptr,
                    instance,
                    nullptr);

            if (!g_overlay)
                return false;

            SetLayeredWindowAttributes(
                g_overlay,
                0,
                225,
                LWA_ALPHA);

            return true;
        }

        void ShowOverlay(
            ULONGLONG durationMs = 2500)
        {
            if (!EnsureOverlay())
                return;

            InvalidateRect(
                g_overlay,
                nullptr,
                TRUE);

            SetWindowPos(
                g_overlay,
                HWND_TOPMOST,
                40,
                118,
                OverlayWidth(),
                OverlayHeight(),
                SWP_NOACTIVATE |
                SWP_SHOWWINDOW);

            g_hideAt =
                GetTickCount64() +
                durationMs;
        }

        void HideOverlay()
        {
            if (!g_overlay)
                return;

            ShowWindow(
                g_overlay,
                SW_HIDE);

            g_hideAt = 0;
            g_showHelp = false;
        }

        void ShowHotkeyHelp()
        {
            g_showHelp = true;
            ShowOverlay(7000);
        }

        void ChangeDelay(
            int deltaMs)
        {
            g_showHelp = false;

            g_delayMs =
                std::clamp(
                    g_delayMs +
                        deltaMs,
                    MIN_DELAY_MS,
                    MAX_DELAY_MS);

            SaveSettings();

            g_status =
                "Delay changed";

            ShowOverlay();
        }
    }

    void Initialize()
    {
        BuildIniPath();

        g_enabled =
            ReadIniBool(
                INI_KEY_ENABLED,
                true);

        g_delayMs =
            std::clamp(
                ReadIniInt(
                    INI_KEY_DELAY,
                    DEFAULT_DELAY_MS),
                MIN_DELAY_MS,
                MAX_DELAY_MS);

        g_debug =
            ReadIniBool(
                INI_KEY_DEBUG,
                false);

        ResetCapture();

        if (g_debug)
        {
            g_lastMenu =
                CurrentMenu();

            g_status =
                g_lastMenu.empty()
                ? "Menu pointer unresolved"
                : "Menu pointer resolved";

            ShowOverlay(3500);
        }
    }

    void Poll()
    {
        if (KeyPressed(VK_F4))
        {
            ShowHotkeyHelp();
        }

        if (KeyPressed(VK_F5))
        {
            g_showHelp = false;
            g_enabled = !g_enabled;

            ResetCapture();
            SaveSettings();

            g_status =
                g_enabled
                ? "Enabled"
                : "Disabled";

            ShowOverlay();
        }

        if (KeyPressed(VK_F6))
        {
            ChangeDelay(
                -DELAY_STEP_MS);
        }

        if (KeyPressed(VK_F7))
        {
            ChangeDelay(
                DELAY_STEP_MS);
        }

        const std::string menu =
            CurrentMenu();

        if (g_debug &&
            menu != g_lastMenu)
        {
            g_lastMenu = menu;

            if (g_lastMenu.empty())
            {
                g_status =
                    "Menu pointer unresolved";
            }
            else if (
                IsScoreMenu(
                    g_lastMenu))
            {
                char statusText[96] = {};

                sprintf_s(
                    statusText,
                    "Score screen detected - capture in %.1fs",
                    g_delayMs /
                        1000.0);

                g_status =
                    statusText;
            }
            else
            {
                g_status =
                    "Menu detected";
            }

            ShowOverlay(3500);
        }

        if (g_overlay &&
            IsWindowVisible(
                g_overlay) &&
            g_hideAt != 0 &&
            GetTickCount64() >=
                g_hideAt)
        {
            HideOverlay();
        }

        if (!g_enabled)
            return;

        if (!IsScoreMenu(menu))
        {
            if (!g_scoreMenu.empty())
                ResetCapture();

            return;
        }

        const ULONGLONG now =
            GetTickCount64();

        if (menu !=
            g_scoreMenu)
        {
            g_scoreMenu =
                menu;

            g_captureAt =
                now +
                static_cast<
                    ULONGLONG>(
                        g_delayMs);

            g_captured = false;

            if (g_debug)
            {
                char statusText[96] = {};

                sprintf_s(
                    statusText,
                    "Score screen detected - capture in %.1fs",
                    g_delayMs /
                        1000.0);

                g_status =
                    statusText;

                ShowOverlay(3500);
            }

            return;
        }

        if (!g_captured &&
            g_captureAt != 0 &&
            now >= g_captureAt)
        {
            if (!IsRocksmithForeground())
            {
                if (g_debug)
                {
                    g_status =
                        "Capture ready - waiting for Rocksmith focus";
                }

                return;
            }

            TakeScreenshot();
            g_captured = true;

            if (g_debug)
            {
                g_status =
                    "Screenshot sent";

                ShowOverlay(3500);
            }
        }
    }

    void Shutdown()
    {
        if (g_overlay)
        {
            DestroyWindow(
                g_overlay);

            g_overlay = nullptr;
        }

        if (g_font)
        {
            DeleteObject(
                g_font);

            g_font = nullptr;
        }

        ResetCapture();
    }
}
