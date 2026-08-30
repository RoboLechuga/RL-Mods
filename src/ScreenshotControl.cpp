#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "ScreenshotControl.hpp"

#pragma comment(lib, "Gdi32.lib")

namespace ScreenshotControl
{
    namespace
    {
        constexpr std::uintptr_t CURRENT_MENU_RELOCATED = 0x00F6062C;
        constexpr std::uintptr_t CURRENT_MENU_LEGACY = 0x0135F62C;
        constexpr ULONGLONG SCREENSHOT_DELAY_MS = 8000;
        constexpr int KEY_TOGGLE = VK_F9;

        constexpr const char* VERSION_TEXT = "RLMods 1.0.1-test5";

        bool g_enabled = true;
        bool g_captured = false;
        ULONGLONG g_captureAt = 0;
        std::string g_scoreMenu;
        std::string g_lastMenu;
        std::string g_status = "Waiting for menu";

        HWND g_overlay = nullptr;
        HFONT g_font = nullptr;
        ULONGLONG g_hideAt = 0;

        bool IsReadableAddress(const void* address)
        {
            if (!address)
                return false;

            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(address, &mbi, sizeof(mbi)))
                return false;

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

            return (mbi.Protect & readable) != 0;
        }

        std::uintptr_t DereferenceAndAdd(
            std::uintptr_t address,
            std::uintptr_t offset)
        {
            if (!IsReadableAddress(
                    reinterpret_cast<const void*>(address)))
            {
                return 0;
            }

            const std::uintptr_t next =
                *reinterpret_cast<const std::uintptr_t*>(address);

            if (!next)
                return 0;

            return next + offset;
        }

        std::uintptr_t ResolveMenuFromBase(std::uintptr_t address)
        {
            address = DereferenceAndAdd(address, 0x28);
            if (!address)
                return 0;

            address = DereferenceAndAdd(address, 0x8C);
            if (!address)
                return 0;

            return DereferenceAndAdd(address, 0x0);
        }

        std::string ReadMenuString(std::uintptr_t address)
        {
            if (!address)
                return {};

            const char* text =
                reinterpret_cast<const char*>(address);

            if (!IsReadableAddress(text))
                return {};

            constexpr size_t MAX_MENU_LENGTH = 128;

            size_t length = 0;
            while (length < MAX_MENU_LENGTH)
            {
                const char* current = text + length;

                if (!IsReadableAddress(current))
                    return {};

                const unsigned char c =
                    static_cast<unsigned char>(*current);

                if (c == '\0')
                    break;

                // Menu identifiers are ordinary printable ASCII.
                // Reject garbage from a bad pointer chain.
                if (c < 32 || c > 126)
                    return {};

                ++length;
            }

            if (length == 0 || length == MAX_MENU_LENGTH)
                return {};

            return std::string(text, length);
        }

        std::string CurrentMenu()
        {
            HMODULE gameModule = GetModuleHandleW(nullptr);
            if (!gameModule)
                return {};

            const std::uintptr_t moduleBase =
                reinterpret_cast<std::uintptr_t>(gameModule);

            // Current Remastered/base-relative candidate.
            std::string menu = ReadMenuString(
                ResolveMenuFromBase(
                    moduleBase + CURRENT_MENU_RELOCATED));

            if (!menu.empty())
                return menu;

            // Legacy absolute candidate used by RSMods' alternate version entry.
            menu = ReadMenuString(
                ResolveMenuFromBase(CURRENT_MENU_LEGACY));

            return menu;
        }

        bool IsScoreMenu(const std::string& menu)
        {
            // RSMods uses Contains(), not exact equality.
            return
                menu.find("LearnASong_SongReview") != std::string::npos ||
                menu.find("ScoreAttack_SongReview") != std::string::npos ||
                menu.find("Duet_SongReview") != std::string::npos ||
                menu.find("H2H_SongReview") != std::string::npos;
        }

        void TakeScreenshot()
        {
            INPUT inputs[2]{};

            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_F12;

            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = VK_F12;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

            SendInput(2, inputs, sizeof(INPUT));
        }

        void ResetCapture()
        {
            g_scoreMenu.clear();
            g_captureAt = 0;
            g_captured = false;
        }

        std::string OverlayText()
        {
            char buffer[512] = {};

            const char* menuText =
                g_lastMenu.empty()
                ? "<unresolved>"
                : g_lastMenu.c_str();

            sprintf_s(
                buffer,
                "Auto Screenshot: %s\nMenu: %s\nStatus: %s\n%s",
                g_enabled ? "ON" : "OFF",
                menuText,
                g_status.c_str(),
                VERSION_TEXT);

            return buffer;
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
                HDC dc = BeginPaint(hwnd, &ps);

                RECT rect{};
                GetClientRect(hwnd, &rect);

                HBRUSH background =
                    CreateSolidBrush(RGB(20, 20, 20));
                FillRect(dc, &rect, background);
                DeleteObject(background);

                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(245, 245, 245));

                HFONT previousFont = nullptr;
                if (g_font)
                {
                    previousFont =
                        reinterpret_cast<HFONT>(
                            SelectObject(dc, g_font));
                }

                std::string text = OverlayText();

                DrawTextA(
                    dc,
                    text.c_str(),
                    -1,
                    &rect,
                    DT_CENTER |
                    DT_VCENTER |
                    DT_NOPREFIX);

                if (previousFont)
                    SelectObject(dc, previousFont);

                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_NCHITTEST:
                return HTTRANSPARENT;

            default:
                return DefWindowProc(
                    hwnd,
                    message,
                    wParam,
                    lParam);
            }
        }

        bool CreateOverlay()
        {
            HINSTANCE instance = GetModuleHandleW(nullptr);
            const wchar_t* className = L"RLModsScreenshotOSD";

            WNDCLASSW wc{};
            wc.lpfnWndProc = OverlayProc;
            wc.hInstance = instance;
            wc.lpszClassName = className;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

            RegisterClassW(&wc);

            g_font = CreateFontW(
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
                DEFAULT_PITCH | FF_DONTCARE,
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
                    145,
                    650,
                    118,
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

        void ShowOverlay(ULONGLONG durationMs = 3000)
        {
            if (!g_overlay)
                return;

            InvalidateRect(g_overlay, nullptr, TRUE);

            SetWindowPos(
                g_overlay,
                HWND_TOPMOST,
                40,
                145,
                650,
                118,
                SWP_NOACTIVATE |
                SWP_SHOWWINDOW);

            g_hideAt = GetTickCount64() + durationMs;
        }

        bool KeyPressed(int virtualKey)
        {
            return (GetAsyncKeyState(virtualKey) & 1) != 0;
        }
    }

    void Initialize()
    {
        g_enabled = true;
        ResetCapture();

        g_lastMenu = CurrentMenu();
        g_status =
            g_lastMenu.empty()
            ? "Menu pointer unresolved"
            : "Menu pointer resolved";

        CreateOverlay();
        ShowOverlay(4000);
    }

    void Poll()
    {
        if (KeyPressed(KEY_TOGGLE))
        {
            g_enabled = !g_enabled;
            ResetCapture();

            g_status =
                g_enabled
                ? "Enabled"
                : "Disabled";

            ShowOverlay();
        }

        const std::string menu = CurrentMenu();

        if (menu != g_lastMenu)
        {
            g_lastMenu = menu;

            if (g_lastMenu.empty())
                g_status = "Menu pointer unresolved";
            else if (IsScoreMenu(g_lastMenu))
                g_status = "Score screen detected - capture in 8s";
            else
                g_status = "Menu detected";

            ShowOverlay(4000);
        }

        if (g_overlay &&
            IsWindowVisible(g_overlay) &&
            g_hideAt != 0 &&
            GetTickCount64() >= g_hideAt)
        {
            ShowWindow(g_overlay, SW_HIDE);
            g_hideAt = 0;
        }

        if (!g_enabled)
            return;

        if (!IsScoreMenu(menu))
        {
            if (!g_scoreMenu.empty())
                ResetCapture();

            return;
        }

        const ULONGLONG now = GetTickCount64();

        if (menu != g_scoreMenu)
        {
            g_scoreMenu = menu;
            g_captureAt = now + SCREENSHOT_DELAY_MS;
            g_captured = false;

            g_status = "Score screen detected - capture in 8s";
            ShowOverlay(4000);
            return;
        }

        if (!g_captured &&
            g_captureAt != 0 &&
            now >= g_captureAt)
        {
            TakeScreenshot();
            g_captured = true;

            g_status = "Screenshot sent";
            ShowOverlay(4000);
        }
    }

    void Shutdown()
    {
        if (g_overlay)
        {
            DestroyWindow(g_overlay);
            g_overlay = nullptr;
        }

        if (g_font)
        {
            DeleteObject(g_font);
            g_font = nullptr;
        }

        ResetCapture();
    }
}
