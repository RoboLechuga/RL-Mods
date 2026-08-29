#include <Windows.h>

#include <cstdint>
#include <string>

#include "ScreenshotControl.hpp"

#pragma comment(lib, "Gdi32.lib")

namespace ScreenshotControl
{
    namespace
    {
        constexpr std::uintptr_t CURRENT_MENU_BASE = 0x00F6062C;
        constexpr ULONGLONG SCREENSHOT_DELAY_MS = 8000;
        constexpr int KEY_TOGGLE = VK_F9;

        bool g_enabled = true;
        bool g_captured = false;
        ULONGLONG g_captureAt = 0;
        std::string g_scoreMenu;

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

        std::uintptr_t ResolveCurrentMenu()
        {
            HMODULE gameModule = GetModuleHandleW(nullptr);
            if (!gameModule)
                return 0;

            std::uintptr_t address =
                reinterpret_cast<std::uintptr_t>(gameModule) +
                CURRENT_MENU_BASE;

            // Same current-menu pointer chain used by RSMods:
            // { 0x28, 0x8C, 0x0 }
            address = DereferenceAndAdd(address, 0x28);
            if (!address)
                return 0;

            address = DereferenceAndAdd(address, 0x8C);
            if (!address)
                return 0;

            address = DereferenceAndAdd(address, 0x0);
            return address;
        }

        std::string CurrentMenu()
        {
            const std::uintptr_t address = ResolveCurrentMenu();
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

                if (*current == '\0')
                    break;

                ++length;
            }

            if (length == 0 || length == MAX_MENU_LENGTH)
                return {};

            return std::string(text, length);
        }

        bool IsScoreMenu(const std::string& menu)
        {
            return
                menu == "LearnASong_SongReview" ||
                menu == "ScoreAttack_SongReview" ||
                menu == "Duet_SongReview" ||
                menu == "H2H_SongReview";
        }

        BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM parameter)
        {
            DWORD processId = 0;
            GetWindowThreadProcessId(hwnd, &processId);

            if (processId != GetCurrentProcessId())
                return TRUE;
            if (!IsWindowVisible(hwnd))
                return TRUE;
            if (GetWindow(hwnd, GW_OWNER) != nullptr)
                return TRUE;

            *reinterpret_cast<HWND*>(parameter) = hwnd;
            return FALSE;
        }

        HWND FindGameWindow()
        {
            HWND hwnd = nullptr;
            EnumWindows(
                FindGameWindowProc,
                reinterpret_cast<LPARAM>(&hwnd));
            return hwnd;
        }

        void TakeScreenshot()
        {
            const HWND gameWindow = FindGameWindow();
            if (!gameWindow)
                return;

            PostMessageW(gameWindow, WM_KEYDOWN, VK_F12, 0);
            Sleep(30);
            PostMessageW(gameWindow, WM_KEYUP, VK_F12, 0);
        }

        void ResetCapture()
        {
            g_scoreMenu.clear();
            g_captureAt = 0;
            g_captured = false;
        }

        const char* OverlayText()
        {
            return g_enabled
                ? "Auto Screenshot: ON"
                : "Auto Screenshot: OFF";
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

                DrawTextA(
                    dc,
                    OverlayText(),
                    -1,
                    &rect,
                    DT_CENTER |
                    DT_VCENTER |
                    DT_SINGLELINE |
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
                -24,
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
                    118,
                    285,
                    58,
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

        void ShowOverlay()
        {
            if (!g_overlay)
                return;

            InvalidateRect(g_overlay, nullptr, TRUE);

            SetWindowPos(
                g_overlay,
                HWND_TOPMOST,
                40,
                118,
                285,
                58,
                SWP_NOACTIVATE |
                SWP_SHOWWINDOW);

            g_hideAt = GetTickCount64() + 1800;
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
        CreateOverlay();
        ShowOverlay();
    }

    void Poll()
    {
        if (KeyPressed(KEY_TOGGLE))
        {
            g_enabled = !g_enabled;
            ResetCapture();
            ShowOverlay();
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

        const std::string menu = CurrentMenu();

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
            return;
        }

        if (!g_captured &&
            g_captureAt != 0 &&
            now >= g_captureAt)
        {
            TakeScreenshot();
            g_captured = true;
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
