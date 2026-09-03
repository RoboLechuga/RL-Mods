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

        constexpr ULONGLONG AUTO_READ_INTERVAL_MS = 100;
        constexpr ULONGLONG AUTO_CANCEL_GRACE_MS = 3000;

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

            // Auto mode deliberately keeps the physical guitar at A440-family
            // reference and lets the shifter supply alternate true tuning.
            int physicalReferenceHz =
                DEFAULT_REFERENCE_HZ;

            int manualShift = 0;
            int manualReferenceHz =
                DEFAULT_REFERENCE_HZ;

            int displayShift = 0;
            int displayReferenceHz =
                DEFAULT_REFERENCE_HZ;

            bool retuneRequired = false;
            bool autoWaiting = false;
            bool autoTargetUnavailable = false;
        };

        struct AutoTunerSession
        {
            bool active = false;
            bool targetValid = false;

            PlayerState previousPlayer{};

            RocksmithTuning::Tuning target{};
            RocksmithTuning::Tuning requiredPhysical{};

            int shift = 0;
            int referenceHz =
                DEFAULT_REFERENCE_HZ;

            ULONGLONG leftTunerAt = 0;
        };

        PlayerState g_players[MAX_PLAYERS];
        ControlMode g_mode = ControlMode::Sync;

        AsioPassthrough::Status g_lastAsioStatus =
            AsioPassthrough::Status::NotInstalled;

        HWND g_overlay = nullptr;
        HFONT g_font = nullptr;
        ULONGLONG g_hideAt = 0;

        AutoTunerSession g_autoSession{};
        ULONGLONG g_nextAutoReadAt = 0;

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
                ApplyManualPlayer(player);
            }
        }

        std::string ShiftName(
            const PlayerState& state)
        {
            if (state.autoTargetUnavailable)
                return "Auto target unavailable";

            if (state.autoWaiting)
                return "Waiting for tuner";

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

        int ChooseAutoShift(
            const RocksmithTuning::Tuning& target)
        {
            // Use the highest-pitched string in the target as the global shift.
            // This mirrors the useful behavior of a drop pedal:
            //   Eb Drop Db -> -1 globally, physical Drop D
            //   D Standard  -> -2 globally, physical E Standard
            //   Drop D/Open G -> 0 globally, physical retune only
            int highest =
                target.strings[0];

            for (size_t i = 1;
                 i < target.strings.size();
                 ++i)
            {
                if (target.strings[i] > highest)
                {
                    highest =
                        target.strings[i];
                }
            }

            return
                std::clamp(
                    highest,
                    MIN_SHIFT,
                    MAX_SHIFT);
        }

        void MarkPlayer2AutoUnavailable()
        {
            if (!AsioPassthrough::
                    IsPlayerReady(1))
            {
                return;
            }

            auto& player2 =
                g_players[1];

            ClearAutoFlags(1);
            player2.autoTargetUnavailable = true;
            player2.displayReferenceHz =
                player2.physicalReferenceHz;

            ApplyPlayerNeutral(1);
        }

        void BeginAutoTunerSession()
        {
            g_autoSession = {};
            g_autoSession.active = true;
            g_autoSession.previousPlayer =
                g_players[0];

            auto& player1 =
                g_players[0];

            ClearAutoFlags(0);
            player1.autoWaiting = true;
            player1.displayReferenceHz =
                player1.physicalReferenceHz;

            // Do not carry the previous song's virtual shift into a new tuner
            // while the new target text is still being populated.
            ApplyPlayerNeutral(0);

            MarkPlayer2AutoUnavailable();
        }

        void ApplyAutoTunerTarget(
            const RocksmithTuning::Tuning& target,
            int referenceHz)
        {
            const int shift =
                ChooseAutoShift(target);

            const RocksmithTuning::Tuning requiredPhysical =
                RocksmithTuning::Shifted(
                    target,
                    -shift);

            g_autoSession.target = target;
            g_autoSession.requiredPhysical =
                requiredPhysical;
            g_autoSession.shift = shift;
            g_autoSession.referenceHz =
                referenceHz;
            g_autoSession.targetValid = true;
            g_autoSession.leftTunerAt = 0;

            auto& player1 =
                g_players[0];

            ClearAutoFlags(0);

            // What the player must physically tune the guitar to after the
            // global virtual shift is removed from the Rocksmith target.
            player1.physical =
                requiredPhysical;
            player1.physicalReferenceHz =
                DEFAULT_REFERENCE_HZ;

            ApplyPlayerTarget(
                0,
                shift,
                referenceHz);

            MarkPlayer2AutoUnavailable();

            std::string notice =
                "Auto target: ";
            notice +=
                RocksmithTuning::Name(target);
            notice +=
                " | Guitar: ";
            notice +=
                RocksmithTuning::Name(
                    requiredPhysical);
            notice +=
                " | Shift: ";
            notice +=
                std::to_string(shift);

            SetNotice(notice);
        }

        void CommitAutoTunerSession()
        {
            if (!g_autoSession.active)
                return;

            auto& player1 =
                g_players[0];

            if (g_autoSession.targetValid)
            {
                // Rocksmith only advances from the tuner after the effective
                // tuning passes. Therefore target - virtual shift is now the
                // confirmed physical topology of the guitar.
                player1.physical =
                    g_autoSession.requiredPhysical;
                player1.physicalReferenceHz =
                    DEFAULT_REFERENCE_HZ;
                player1.manualShift = 0;
                player1.manualReferenceHz =
                    DEFAULT_REFERENCE_HZ;

                ClearAutoFlags(0);

                std::string notice =
                    "Auto locked: ";
                notice +=
                    RocksmithTuning::Name(
                        g_autoSession.target);
                notice +=
                    " | Shift: ";
                notice +=
                    std::to_string(
                        g_autoSession.shift);

                SetNotice(notice);
            }
            else
            {
                // Unknown/custom tuner text: stay neutral and let Rocksmith's
                // tuner be the authority. Future Auto decisions do not depend
                // on this remembered topology.
                ClearAutoFlags(0);
                player1.retuneRequired = true;
                SetNotice(
                    "Auto bypassed unknown/custom tuning");
            }

            g_autoSession = {};
        }

        void CancelAutoTunerSession()
        {
            if (!g_autoSession.active)
                return;

            const PlayerState previous =
                g_autoSession.previousPlayer;

            g_players[0] = previous;

            AsioPassthrough::SetPlayerRatio(
                0,
                RatioForRelativeTarget(
                    previous.displayShift,
                    previous.displayReferenceHz,
                    previous.physicalReferenceHz));

            g_autoSession = {};

            SetNotice(
                "Tuner cancelled; previous shift restored");
        }

        void ResetAutoState()
        {
            g_autoSession = {};
            g_nextAutoReadAt = 0;

            for (int player = 0;
                 player < MAX_PLAYERS;
                 ++player)
            {
                ClearAutoFlags(player);
                g_players[player]
                    .displayReferenceHz =
                    g_players[player]
                        .physicalReferenceHz;
                ApplyPlayerNeutral(player);
            }

            g_players[0]
                .autoWaiting = true;

            MarkPlayer2AutoUnavailable();
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

            const std::string menu =
                RocksmithTuning::
                    CurrentMenuName();

            if (menu.empty())
                return false;

            // SelectionListDialog is generic and can be used outside a real
            // pre-song tuner, so do not let it start an Auto session by itself.
            const bool inPreSongTuner =
                menu != "SelectionListDialog" &&
                RocksmithTuning::
                    IsPreSongTuner(menu);

            bool changed = false;

            if (inPreSongTuner)
            {
                if (!g_autoSession.active)
                {
                    BeginAutoTunerSession();
                    changed = true;
                }

                g_autoSession.leftTunerAt = 0;

                RocksmithTuning::Tuning target{};

                if (RocksmithTuning::
                        TryReadTunerTarget(
                            target))
                {
                    int referenceHz =
                        DEFAULT_REFERENCE_HZ;

                    RocksmithTuning::
                        TryReadReferenceHz(
                            referenceHz);

                    if (!g_autoSession.targetValid ||
                        target !=
                            g_autoSession.target ||
                        referenceHz !=
                            g_autoSession.referenceHz)
                    {
                        ApplyAutoTunerTarget(
                            target,
                            referenceHz);

                        changed = true;
                    }
                }

                return changed;
            }

            if (!g_autoSession.active)
            {
                // No tuner event: keep the current virtual shift exactly as-is.
                // This is essential when Nonstop Play skips a tuner because
                // Rocksmith believes the effective tuning has not changed.
                return false;
            }

            if (RocksmithTuning::
                    IsSongGameplayMenu(menu))
            {
                CommitAutoTunerSession();
                return true;
            }

            if (g_autoSession.leftTunerAt == 0)
            {
                g_autoSession.leftTunerAt = now;
                return false;
            }

            // Give loading/intermediate menus time to resolve to *_Game. If the
            // player backed out of the tuner instead, restore the previous shift.
            if (now -
                    g_autoSession.leftTunerAt >=
                AUTO_CANCEL_GRACE_MS)
            {
                CancelAutoTunerSession();
                return true;
            }

            return false;
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
                g_autoSession = {};
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

                    ApplyManualPlayer(player);
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

                    ApplyManualPlayer(player);
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

                    ApplyManualPlayer(player);
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

        // F10 is intentionally unassigned. Auto confirmation now comes from
        // successfully leaving a Rocksmith pre-song tuner into *_Game.

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
