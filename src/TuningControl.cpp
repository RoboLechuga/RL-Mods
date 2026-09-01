#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "TuningControl.hpp"
#include "AsioPassthrough.hpp"
#include "RocksmithTuning.hpp"

#pragma comment(lib, "Gdi32.lib")

namespace TuningControl
{
    namespace
    {
        constexpr int MAX_PLAYERS = 2;

        constexpr int MIN_SHIFT = -12;
        constexpr int MAX_SHIFT = 0;
        constexpr int MIN_REFERENCE_HZ = 420;
        constexpr int MAX_REFERENCE_HZ = 461;
        constexpr int DEFAULT_REFERENCE_HZ = 440;

        constexpr int KEY_SHIFT_DOWN = VK_OEM_COMMA;
        constexpr int KEY_SHIFT_UP   = VK_OEM_PERIOD;
        constexpr int KEY_REF_DOWN   = VK_OEM_1;
        constexpr int KEY_REF_UP     = VK_OEM_7;
        constexpr int KEY_REF_RESET  = VK_OEM_5;

        constexpr ULONGLONG AUTO_READ_INTERVAL_MS = 250;
        constexpr ULONGLONG AUTO_LOST_GRACE_MS = 1000;

        enum class ControlMode
        {
            Player1,
            Player2,
            Sync,
            Auto
        };

        struct PlayerState
        {
            RocksmithTuning::Tuning physical{};

            // The reference the physical guitar was actually tuned against.
            int physicalReferenceHz =
                DEFAULT_REFERENCE_HZ;

            // Manual target relative to the physical baseline.
            int manualShift = 0;
            int manualReferenceHz =
                DEFAULT_REFERENCE_HZ;

            // Current display/effective target.
            int displayShift = 0;
            int displayReferenceHz =
                DEFAULT_REFERENCE_HZ;

            bool retuneRequired = false;
            bool autoWaiting = false;
            bool autoTargetUnavailable = false;
        };

        PlayerState g_players[MAX_PLAYERS];
        ControlMode g_mode = ControlMode::Sync;

        AsioPassthrough::Status g_lastAsioStatus =
            AsioPassthrough::Status::NotInstalled;

        HWND g_overlay = nullptr;
        HFONT g_font = nullptr;
        ULONGLONG g_hideAt = 0;

        RocksmithTuning::Tuning g_pendingAutoTuning{};
        int g_pendingAutoReferenceHz =
            DEFAULT_REFERENCE_HZ;
        int g_pendingAutoMatches = 0;

        RocksmithTuning::Tuning g_autoTuning{};
        int g_autoReferenceHz =
            DEFAULT_REFERENCE_HZ;
        bool g_autoTargetValid = false;

        ULONGLONG g_nextAutoReadAt = 0;
        ULONGLONG g_lastAutoReadAt = 0;

        std::string g_notice;
        ULONGLONG g_noticeUntil = 0;

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
            // Consume the latch whether focused or not.
            const SHORT state =
                GetAsyncKeyState(
                    virtualKey);

            if ((state & 1) == 0)
                return false;

            return IsRocksmithForeground();
        }

        const char* ModeName()
        {
            switch (g_mode)
            {
            case ControlMode::Player1:
                return "Player 1";

            case ControlMode::Player2:
                return "Player 2";

            case ControlMode::Sync:
                return "Sync";

            case ControlMode::Auto:
                return "Auto";

            default:
                return "Unknown";
            }
        }

        void SetNotice(
            const std::string& text,
            ULONGLONG durationMs = 3000)
        {
            g_notice = text;
            g_noticeUntil =
                GetTickCount64() +
                durationMs;
        }

        void ClearAutoFlags(
            int player)
        {
            g_players[player]
                .retuneRequired = false;

            g_players[player]
                .autoWaiting = false;

            g_players[player]
                .autoTargetUnavailable = false;
        }

        float RatioForRelativeTarget(
            int semitones,
            int targetReferenceHz,
            int physicalReferenceHz)
        {
            if (targetReferenceHz < 1)
                targetReferenceHz =
                    DEFAULT_REFERENCE_HZ;

            if (physicalReferenceHz < 1)
                physicalReferenceHz =
                    DEFAULT_REFERENCE_HZ;

            const float coarse =
                std::pow(
                    2.0f,
                    static_cast<float>(
                        semitones) /
                    12.0f);

            const float reference =
                static_cast<float>(
                    targetReferenceHz) /
                static_cast<float>(
                    physicalReferenceHz);

            return coarse * reference;
        }

        void ApplyPlayerTarget(
            int player,
            int semitones,
            int targetReferenceHz)
        {
            auto& state =
                g_players[player];

            state.displayShift =
                semitones;

            state.displayReferenceHz =
                targetReferenceHz;

            AsioPassthrough::SetPlayerRatio(
                player,
                RatioForRelativeTarget(
                    semitones,
                    targetReferenceHz,
                    state.physicalReferenceHz));
        }

        void ApplyPlayerNeutral(
            int player)
        {
            AsioPassthrough::SetPlayerRatio(
                player,
                1.0f);

            g_players[player]
                .displayShift = 0;
        }

        void ApplyManualPlayer(
            int player)
        {
            auto& state =
                g_players[player];

            ClearAutoFlags(player);

            ApplyPlayerTarget(
                player,
                state.manualShift,
                state.manualReferenceHz);
        }

        void ApplyAllManual()
        {
            for (int player = 0;
                 player < MAX_PLAYERS;
                 ++player)
            {
                ApplyManualPlayer(
                    player);
            }
        }

        std::string ShiftName(
            const PlayerState& state)
        {
            if (state.autoTargetUnavailable)
                return "Auto target unavailable";

            if (state.autoWaiting)
                return "Waiting for arrangement";

            if (state.retuneRequired)
                return "Guitar retune required";

            return
                RocksmithTuning::Name(
                    RocksmithTuning::Shifted(
                        state.physical,
                        state.displayShift));
        }

        bool SameDisplayState(
            const PlayerState& a,
            const PlayerState& b)
        {
            return
                a.physical == b.physical &&
                a.displayShift ==
                    b.displayShift &&
                a.displayReferenceHz ==
                    b.displayReferenceHz &&
                a.retuneRequired ==
                    b.retuneRequired &&
                a.autoWaiting ==
                    b.autoWaiting &&
                a.autoTargetUnavailable ==
                    b.autoTargetUnavailable;
        }

        std::string PlayerBlock(
            const char* label,
            const PlayerState& state)
        {
            char buffer[320] = {};

            sprintf_s(
                buffer,
                "%s  Guitar Tuning: %s\n"
                "Shift Tuning: %s\n"
                "Reference: A%d",
                label,
                RocksmithTuning::Name(
                    state.physical).c_str(),
                ShiftName(state).c_str(),
                state.displayReferenceHz);

            return buffer;
        }

        std::string CurrentText()
        {
            const auto asioStatus =
                AsioPassthrough::GetStatus();

            if (asioStatus !=
                    AsioPassthrough::Status::Ready &&
                asioStatus !=
                    AsioPassthrough::Status::WaitingForBuffers)
            {
                return
                    AsioPassthrough::GetStatusText();
            }

            std::string text =
                "Mode: ";

            text += ModeName();
            text += "\n";

            const bool player1Ready =
                AsioPassthrough::IsPlayerReady(0);

            const bool player2Ready =
                AsioPassthrough::IsPlayerReady(1);

            if (g_mode ==
                ControlMode::Player1)
            {
                text +=
                    PlayerBlock(
                        "P1",
                        g_players[0]);
            }
            else if (g_mode ==
                ControlMode::Player2)
            {
                text +=
                    PlayerBlock(
                        "P2",
                        g_players[1]);

                if (!player2Ready)
                {
                    text +=
                        "\nP2 ASIO input is not ready";
                }
            }
            else if (player2Ready &&
                     SameDisplayState(
                         g_players[0],
                         g_players[1]))
            {
                text +=
                    PlayerBlock(
                        "P1/P2",
                        g_players[0]);
            }
            else
            {
                text +=
                    PlayerBlock(
                        "P1",
                        g_players[0]);

                if (player2Ready)
                {
                    text += "\n\n";
                    text +=
                        PlayerBlock(
                            "P2",
                            g_players[1]);
                }
                else if (!player1Ready)
                {
                    text +=
                        "\nASIO input is not ready";
                }
            }

            if (!g_notice.empty() &&
                GetTickCount64() <
                    g_noticeUntil)
            {
                text += "\n\n";
                text += g_notice;
            }

            return text;
        }

        int OverlayHeight()
        {
            const std::string text =
                CurrentText();

            int lines = 1;

            for (char c : text)
            {
                if (c == '\n')
                    ++lines;
            }

            return
                std::clamp(
                    24 + lines * 27,
                    96,
                    280);
        }

        void ApplyAutoTarget(
            const RocksmithTuning::Tuning& target,
            int referenceHz)
        {
            auto& player1 =
                g_players[0];

            ClearAutoFlags(0);

            int shift = 0;

            if (!RocksmithTuning::
                    TryGetUniformShift(
                        player1.physical,
                        target,
                        shift) ||
                shift < MIN_SHIFT ||
                shift > MAX_SHIFT)
            {
                player1.retuneRequired =
                    true;

                player1.displayReferenceHz =
                    referenceHz;

                ApplyPlayerNeutral(0);
            }
            else
            {
                ApplyPlayerTarget(
                    0,
                    shift,
                    referenceHz);
            }

            const bool player2Ready =
                AsioPassthrough::
                    IsPlayerReady(1);

            if (player2Ready)
            {
                auto& player2 =
                    g_players[1];

                ClearAutoFlags(1);

                // The currently verified Rocksmith arrangement pointer is a
                // single target. We do not apply it to P2 and risk silently
                // pitching the second player for P1's arrangement.
                player2.autoTargetUnavailable =
                    true;

                player2.displayReferenceHz =
                    player2.physicalReferenceHz;

                ApplyPlayerNeutral(1);
            }
        }

        void ResetAutoState()
        {
            g_pendingAutoMatches = 0;
            g_autoTargetValid = false;
            g_nextAutoReadAt = 0;
            g_lastAutoReadAt = 0;

            for (int player = 0;
                 player < MAX_PLAYERS;
                 ++player)
            {
                ClearAutoFlags(player);
                g_players[player]
                    .autoWaiting = true;
                g_players[player]
                    .displayReferenceHz =
                    g_players[player]
                        .physicalReferenceHz;

                ApplyPlayerNeutral(
                    player);
            }

            if (AsioPassthrough::
                    IsPlayerReady(1))
            {
                g_players[1]
                    .autoWaiting = false;

                g_players[1]
                    .autoTargetUnavailable =
                    true;
            }
        }

        bool UpdateAuto()
        {
            if (g_mode !=
                ControlMode::Auto)
            {
                return false;
            }

            const ULONGLONG now =
                GetTickCount64();

            if (now <
                g_nextAutoReadAt)
            {
                return false;
            }

            g_nextAutoReadAt =
                now +
                AUTO_READ_INTERVAL_MS;

            RocksmithTuning::Tuning tuning{};

            if (!RocksmithTuning::
                    TryReadArrangement(
                        tuning))
            {
                if (g_autoTargetValid &&
                    g_lastAutoReadAt != 0 &&
                    now -
                        g_lastAutoReadAt >
                        AUTO_LOST_GRACE_MS)
                {
                    ResetAutoState();
                    return true;
                }

                return false;
            }

            int referenceHz =
                DEFAULT_REFERENCE_HZ;

            RocksmithTuning::
                TryReadReferenceHz(
                    referenceHz);

            g_lastAutoReadAt = now;

            if (g_pendingAutoMatches == 0 ||
                tuning !=
                    g_pendingAutoTuning ||
                referenceHz !=
                    g_pendingAutoReferenceHz)
            {
                g_pendingAutoTuning =
                    tuning;

                g_pendingAutoReferenceHz =
                    referenceHz;

                g_pendingAutoMatches = 1;
                return false;
            }

            ++g_pendingAutoMatches;

            if (g_pendingAutoMatches < 2)
                return false;

            if (g_autoTargetValid &&
                tuning ==
                    g_autoTuning &&
                referenceHz ==
                    g_autoReferenceHz)
            {
                return false;
            }

            g_autoTuning = tuning;
            g_autoReferenceHz =
                referenceHz;
            g_autoTargetValid = true;

            ApplyAutoTarget(
                g_autoTuning,
                g_autoReferenceHz);

            return true;
        }

        void CycleMode()
        {
            switch (g_mode)
            {
            case ControlMode::Player1:
                g_mode =
                    ControlMode::Player2;
                break;

            case ControlMode::Player2:
                g_mode =
                    ControlMode::Sync;
                break;

            case ControlMode::Sync:
                g_mode =
                    ControlMode::Auto;
                ResetAutoState();
                break;

            case ControlMode::Auto:
                g_mode =
                    ControlMode::Player1;
                ApplyAllManual();
                break;
            }

            SetNotice(
                std::string(
                    "Tuning control: ") +
                ModeName());
        }

        void ChangeShift(
            int delta)
        {
            if (g_mode ==
                ControlMode::Auto)
            {
                SetNotice(
                    "Auto controls tuning; press F9 for manual mode");
                return;
            }

            auto changePlayer =
                [delta](int player)
                {
                    auto& state =
                        g_players[player];

                    state.manualShift =
                        std::clamp(
                            state.manualShift +
                                delta,
                            MIN_SHIFT,
                            MAX_SHIFT);

                    ApplyManualPlayer(
                        player);
                };

            if (g_mode ==
                ControlMode::Player1)
            {
                changePlayer(0);
            }
            else if (g_mode ==
                ControlMode::Player2)
            {
                changePlayer(1);
            }
            else
            {
                changePlayer(0);
                changePlayer(1);
            }
        }

        void ChangeReference(
            int delta)
        {
            if (g_mode ==
                ControlMode::Auto)
            {
                SetNotice(
                    "Auto controls reference; press F9 for manual mode");
                return;
            }

            auto changePlayer =
                [delta](int player)
                {
                    auto& state =
                        g_players[player];

                    state.manualReferenceHz =
                        std::clamp(
                            state.manualReferenceHz +
                                delta,
                            MIN_REFERENCE_HZ,
                            MAX_REFERENCE_HZ);

                    ApplyManualPlayer(
                        player);
                };

            if (g_mode ==
                ControlMode::Player1)
            {
                changePlayer(0);
            }
            else if (g_mode ==
                ControlMode::Player2)
            {
                changePlayer(1);
            }
            else
            {
                changePlayer(0);
                changePlayer(1);
            }
        }

        void ResetReference()
        {
            if (g_mode ==
                ControlMode::Auto)
            {
                SetNotice(
                    "Auto controls reference; press F9 for manual mode");
                return;
            }

            auto resetPlayer =
                [](int player)
                {
                    g_players[player]
                        .manualReferenceHz =
                        DEFAULT_REFERENCE_HZ;

                    ApplyManualPlayer(
                        player);
                };

            if (g_mode ==
                ControlMode::Player1)
            {
                resetPlayer(0);
            }
            else if (g_mode ==
                ControlMode::Player2)
            {
                resetPlayer(1);
            }
            else
            {
                resetPlayer(0);
                resetPlayer(1);
            }
        }

        bool ConfirmPlayerPhysicalTuning(
            int player,
            const RocksmithTuning::Tuning& tuning,
            int referenceHz)
        {
            if (player < 0 ||
                player >= MAX_PLAYERS)
            {
                return false;
            }

            auto& state =
                g_players[player];

            state.physical =
                tuning;

            state.physicalReferenceHz =
                referenceHz;

            state.manualShift = 0;
            state.manualReferenceHz =
                referenceHz;

            if (g_mode ==
                ControlMode::Auto &&
                g_autoTargetValid &&
                player == 0)
            {
                ApplyAutoTarget(
                    g_autoTuning,
                    g_autoReferenceHz);
            }
            else
            {
                ApplyManualPlayer(
                    player);
            }

            return true;
        }

        void ConfirmPhysicalTuning()
        {
            RocksmithTuning::Tuning tuning{};

            if (!RocksmithTuning::
                    TryReadArrangement(
                        tuning))
            {
                SetNotice(
                    "No arrangement tuning available to confirm");
                return;
            }

            int referenceHz =
                DEFAULT_REFERENCE_HZ;

            RocksmithTuning::
                TryReadReferenceHz(
                    referenceHz);

            if (g_mode ==
                ControlMode::Player2)
            {
                // Research note: the verified pointer is not independently
                // attributable to P2 in multiplayer. The user may explicitly
                // choose P2 mode to test/confirm that the displayed tuner is
                // P2's target, but we make that choice visible.
                ConfirmPlayerPhysicalTuning(
                    1,
                    tuning,
                    referenceHz);

                SetNotice(
                    "P2 baseline confirmed from current arrangement (experimental)");
                return;
            }

            if ((g_mode ==
                    ControlMode::Sync ||
                 g_mode ==
                    ControlMode::Auto) &&
                AsioPassthrough::
                    IsPlayerReady(1))
            {
                SetNotice(
                    "Select Player 1 or Player 2 with F9, then press F10");
                return;
            }

            ConfirmPlayerPhysicalTuning(
                0,
                tuning,
                referenceHz);

            SetNotice(
                std::string(
                    "P1 guitar baseline confirmed: ") +
                RocksmithTuning::Name(
                    tuning) +
                " / A" +
                std::to_string(
                    referenceHz));
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
                    CurrentText();

                DrawTextA(
                    dc,
                    text.c_str(),
                    -1,
                    &textRect,
                    DT_LEFT |
                    DT_VCENTER |
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
                L"RLModsTuningOSD";

            WNDCLASSW wc{};
            wc.lpfnWndProc = OverlayProc;
            wc.hInstance = instance;
            wc.lpszClassName = className;
            wc.hCursor =
                LoadCursor(
                    nullptr,
                    IDC_ARROW);

            RegisterClassW(&wc);

            g_font =
                CreateFontW(
                    -22,
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
                    40,
                    680,
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

        void ShowOverlay()
        {
            if (!g_overlay)
                return;

            const int height =
                OverlayHeight();

            InvalidateRect(
                g_overlay,
                nullptr,
                TRUE);

            SetWindowPos(
                g_overlay,
                HWND_TOPMOST,
                40,
                40,
                680,
                height,
                SWP_NOACTIVATE |
                SWP_SHOWWINDOW);

            g_hideAt =
                GetTickCount64() +
                2200;
        }
    }

    bool Initialize()
    {
        ApplyAllManual();
        CreateOverlay();

        g_lastAsioStatus =
            AsioPassthrough::GetStatus();

        ShowOverlay();
        return true;
    }

    void Poll()
    {
        bool showOverlay = false;

        if (KeyPressed(VK_F9))
        {
            CycleMode();
            showOverlay = true;
        }

        if (KeyPressed(VK_F10))
        {
            ConfirmPhysicalTuning();
            showOverlay = true;
        }

        if (KeyPressed(
                KEY_SHIFT_DOWN))
        {
            ChangeShift(-1);
            showOverlay = true;
        }

        if (KeyPressed(
                KEY_SHIFT_UP))
        {
            ChangeShift(1);
            showOverlay = true;
        }

        if (KeyPressed(
                KEY_REF_DOWN))
        {
            ChangeReference(-1);
            showOverlay = true;
        }

        if (KeyPressed(
                KEY_REF_UP))
        {
            ChangeReference(1);
            showOverlay = true;
        }

        if (KeyPressed(
                KEY_REF_RESET))
        {
            ResetReference();
            showOverlay = true;
        }

        if (UpdateAuto())
            showOverlay = true;

        const auto asioStatus =
            AsioPassthrough::GetStatus();

        if (asioStatus !=
            g_lastAsioStatus)
        {
            g_lastAsioStatus =
                asioStatus;

            showOverlay = true;
        }

        if (!g_notice.empty() &&
            g_noticeUntil != 0 &&
            GetTickCount64() >=
                g_noticeUntil)
        {
            g_notice.clear();
            g_noticeUntil = 0;

            if (g_overlay &&
                IsWindowVisible(
                    g_overlay))
            {
                showOverlay = true;
            }
        }

        if (showOverlay)
            ShowOverlay();

        if (g_overlay &&
            IsWindowVisible(g_overlay) &&
            g_hideAt != 0 &&
            GetTickCount64() >=
                g_hideAt)
        {
            ShowWindow(
                g_overlay,
                SW_HIDE);

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
