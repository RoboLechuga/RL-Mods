#include <Windows.h>
#include <string>
#include <cstdio>

#include "TuningControl.hpp"
#include "AsioPassthrough.hpp"

#pragma comment(lib, "Gdi32.lib")

namespace TuningControl
{
    namespace
    {
        constexpr int MIN_DROP = -12;
        constexpr int MAX_DROP = 0;
        constexpr int MIN_REFERENCE_HZ = 420;
        constexpr int MAX_REFERENCE_HZ = 461;
        constexpr int DEFAULT_REFERENCE_HZ = 440;

        constexpr const char* VERSION_TEXT = "RLMods 1.0.1-test4";

        constexpr int KEY_DROP_DOWN = VK_OEM_COMMA;
        constexpr int KEY_DROP_UP   = VK_OEM_PERIOD;
        constexpr int KEY_REF_DOWN  = VK_OEM_1;
        constexpr int KEY_REF_UP    = VK_OEM_7;
        constexpr int KEY_REF_RESET = VK_OEM_5;

        int g_dropSemitones = 0;
        int g_referenceHz = DEFAULT_REFERENCE_HZ;

        HWND g_overlay = nullptr;
        HFONT g_font = nullptr;
        ULONGLONG g_hideAt = 0;

        const char* DropName(int semitones)
        {
            static const char* names[] =
            {
                "E", "Eb", "D", "C#", "C", "B", "Bb",
                "A", "Ab", "G", "F#", "F", "E"
            };

            int index = -semitones;
            if (index < 0) index = 0;
            if (index > 12) index = 12;
            return names[index];
        }

        std::string CurrentText()
        {
            char buffer[160] = {};

            sprintf_s(
                buffer,
                "%s | Drop: %s    Ref: A%d",
                VERSION_TEXT,
                DropName(g_dropSemitones),
                g_referenceHz);

            return buffer;
        }

        void Apply()
        {
            AsioPassthrough::SetTuning(
                g_dropSemitones,
                g_referenceHz);
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

                std::string text = CurrentText();

                DrawTextA(
                    dc,
                    text.c_str(),
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
            HINSTANCE instance =
                GetModuleHandleW(nullptr);

            const wchar_t* className =
                L"RSSongRefreshTuningOSD";

            WNDCLASSW wc{};
            wc.lpfnWndProc = OverlayProc;
            wc.hInstance = instance;
            wc.lpszClassName = className;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

            RegisterClassW(&wc);

            g_font = CreateFontW(
                -26,
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
                    40,
                    520,
                    68,
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
                40,
                520,
                68,
                SWP_NOACTIVATE |
                SWP_SHOWWINDOW);

            g_hideAt = GetTickCount64() + 1800;
        }

        bool KeyPressed(int virtualKey)
        {
            return (GetAsyncKeyState(virtualKey) & 1) != 0;
        }
    }

    bool Initialize()
    {
        Apply();
        CreateOverlay();
        ShowOverlay();
        return true;
    }

    void Poll()
    {
        bool changed = false;

        if (KeyPressed(KEY_DROP_DOWN))
        {
            if (g_dropSemitones > MIN_DROP)
            {
                --g_dropSemitones;
                changed = true;
            }
        }

        if (KeyPressed(KEY_DROP_UP))
        {
            if (g_dropSemitones < MAX_DROP)
            {
                ++g_dropSemitones;
                changed = true;
            }
        }

        if (KeyPressed(KEY_REF_DOWN))
        {
            if (g_referenceHz > MIN_REFERENCE_HZ)
            {
                --g_referenceHz;
                changed = true;
            }
        }

        if (KeyPressed(KEY_REF_UP))
        {
            if (g_referenceHz < MAX_REFERENCE_HZ)
            {
                ++g_referenceHz;
                changed = true;
            }
        }

        if (KeyPressed(KEY_REF_RESET))
        {
            if (g_referenceHz != DEFAULT_REFERENCE_HZ)
            {
                g_referenceHz = DEFAULT_REFERENCE_HZ;
                changed = true;
            }
            else
            {
                ShowOverlay();
            }
        }

        if (changed)
        {
            Apply();
            ShowOverlay();
        }

        MSG message{};

        while (PeekMessage(
            &message,
            nullptr,
            0,
            0,
            PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        if (g_overlay &&
            IsWindowVisible(g_overlay) &&
            g_hideAt != 0 &&
            GetTickCount64() >= g_hideAt)
        {
            ShowWindow(g_overlay, SW_HIDE);
            g_hideAt = 0;
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
    }
}
