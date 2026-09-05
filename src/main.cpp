#define WIN32_LEAN_AND_MEAN
#include "AsioPassthrough.hpp"
#include "TuningControl.hpp"
#include "RocksmithTuning.hpp"
#include "ScreenshotControl.hpp"
#include <windows.h>
#include <Xinput.h>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "User32.lib")

namespace
{
    HMODULE g_realXInput = nullptr;
    FARPROC g_proxy[7] = {};
    volatile LONG g_running = TRUE;

    enum ProxyIndex
    {
        Enable = 0,
        GetBatteryInformation,
        GetCapabilities,
        GetDSoundAudioDeviceGuids,
        GetKeystroke,
        GetState,
        SetState
    };

    bool InitRealXInput()
    {
        if (g_realXInput)
            return true;

        char path[MAX_PATH] = {};
        if (!GetSystemDirectoryA(path, MAX_PATH))
            return false;

        if (strcat_s(path, "\\xinput1_3.dll") != 0)
            return false;

        g_realXInput = LoadLibraryA(path);
        if (!g_realXInput)
            return false;

        g_proxy[Enable] = GetProcAddress(g_realXInput, "XInputEnable");
        g_proxy[GetBatteryInformation] = GetProcAddress(g_realXInput, "XInputGetBatteryInformation");
        g_proxy[GetCapabilities] = GetProcAddress(g_realXInput, "XInputGetCapabilities");
        g_proxy[GetDSoundAudioDeviceGuids] = GetProcAddress(g_realXInput, "XInputGetDSoundAudioDeviceGuids");
        g_proxy[GetKeystroke] = GetProcAddress(g_realXInput, "XInputGetKeystroke");
        g_proxy[GetState] = GetProcAddress(g_realXInput, "XInputGetState");
        g_proxy[SetState] = GetProcAddress(g_realXInput, "XInputSetState");

        for (auto p : g_proxy)
        {
            if (!p)
                return false;
        }

        return true;
    }

    bool IsRocksmithForeground()
    {
        HWND foreground = GetForegroundWindow();
        if (!foreground)
            return false;

        DWORD processId = 0;
        GetWindowThreadProcessId(foreground, &processId);

        return processId == GetCurrentProcessId();
    }

    bool KeyPressedInRocksmith(int virtualKey)
    {
        // Always consume GetAsyncKeyState's low-bit latch, even while Rocksmith
        // is unfocused, so presses do not fire later when focus returns.
        const SHORT state = GetAsyncKeyState(virtualKey);

        if ((state & 1) == 0)
            return false;

        return IsRocksmithForeground();
    }

    bool IsReadableAddress(const void* address)
    {
        if (!address)
            return false;

        MEMORY_BASIC_INFORMATION mbi = {};
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

    bool IsWritableAddress(const void* address)
    {
        if (!address)
            return false;

        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(address, &mbi, sizeof(mbi)))
            return false;

        if (mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & PAGE_GUARD)
            return false;
        if (mbi.Protect & PAGE_NOACCESS)
            return false;

        const DWORD writable =
            PAGE_READWRITE |
            PAGE_WRITECOPY |
            PAGE_EXECUTE_READWRITE |
            PAGE_EXECUTE_WRITECOPY;

        return (mbi.Protect & writable) != 0;
    }

    bool IsOffsetInsideModule(
        std::uintptr_t base,
        std::uintptr_t offset,
        size_t bytes)
    {
        if (!base)
            return false;

        const auto* dos =
            reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

        if (!IsReadableAddress(dos) ||
            dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        const auto* nt =
            reinterpret_cast<const IMAGE_NT_HEADERS*>(
                base + static_cast<std::uintptr_t>(dos->e_lfanew));

        if (!IsReadableAddress(nt) ||
            nt->Signature != IMAGE_NT_SIGNATURE)
            return false;

        const std::uintptr_t imageSize =
            static_cast<std::uintptr_t>(
                nt->OptionalHeader.SizeOfImage);

        if (offset >= imageSize)
            return false;

        return bytes <= imageSize - offset;
    }

    std::uintptr_t ResolveEnumerationFlags()
    {
        constexpr std::uintptr_t ENUMERATION_ROOT_OFFSET = 0x00F74E90;

        HMODULE gameModule = GetModuleHandleW(nullptr);
        if (!gameModule)
            return 0;

        const std::uintptr_t base =
            reinterpret_cast<std::uintptr_t>(gameModule);

        if (!IsOffsetInsideModule(
                base,
                ENUMERATION_ROOT_OFFSET,
                sizeof(std::uintptr_t)))
        {
            return 0;
        }

        std::uintptr_t addr =
            base + ENUMERATION_ROOT_OFFSET;

        if (!IsReadableAddress(reinterpret_cast<void*>(addr)))
            return 0;

        addr = *reinterpret_cast<std::uintptr_t*>(addr);
        if (!addr)
            return 0;

        addr += 0x8;

        if (!IsReadableAddress(reinterpret_cast<void*>(addr)))
            return 0;

        addr = *reinterpret_cast<std::uintptr_t*>(addr);
        if (!addr)
            return 0;

        addr += 0x4;
        return addr;
    }

    void ForceEnumeration()
    {
        const std::uintptr_t flags = ResolveEnumerationFlags();
        if (!flags)
            return;

        BYTE* flag1 = reinterpret_cast<BYTE*>(flags);
        BYTE* flag2 = reinterpret_cast<BYTE*>(flags + 4);

        if (!IsReadableAddress(flag1) ||
            !IsReadableAddress(flag2) ||
            !IsWritableAddress(flag1) ||
            !IsWritableAddress(flag2))
        {
            return;
        }

        const BYTE current1 =
            *reinterpret_cast<volatile BYTE*>(flag1);

        const BYTE current2 =
            *reinterpret_cast<volatile BYTE*>(flag2);

        // A stale pointer landing on arbitrary writable memory should no-op
        // rather than corrupt game state.
        if (current1 > 1 || current2 > 1)
            return;

        *reinterpret_cast<volatile BYTE*>(flag1) = 1;
        *reinterpret_cast<volatile BYTE*>(flag2) = 1;
    }

    void PumpMessages()
    {
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
    }

    DWORD WINAPI WorkerThread(void*)
    {
        AsioPassthrough::Install();
        TuningControl::Initialize();
        ScreenshotControl::Initialize();

        while (InterlockedCompareExchange(
            &g_running,
            TRUE,
            TRUE))
        {
            if (KeyPressedInRocksmith(VK_F8))
                ForceEnumeration();

            // Temporary multiplayer tuning diagnostic for v1.3.
            // F8 remains song re-enumeration; F10 only appends a memory snapshot.
            if (KeyPressedInRocksmith(VK_F10))
                RocksmithTuning::CaptureDebugSnapshot();

            ScreenshotControl::Poll();
            TuningControl::Poll();
            PumpMessages();

            Sleep(25);
        }

        ScreenshotControl::Shutdown();
        TuningControl::Shutdown();
        return 0;
    }
}

using T_XInputEnable = void(__stdcall*)(BOOL);
using T_XInputGetBatteryInformation = DWORD(__stdcall*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
using T_XInputGetCapabilities = DWORD(__stdcall*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
using T_XInputGetDSoundAudioDeviceGuids = DWORD(__stdcall*)(DWORD, GUID*, GUID*);
using T_XInputGetKeystroke = DWORD(__stdcall*)(DWORD, DWORD, XINPUT_KEYSTROKE*);
using T_XInputGetState = DWORD(__stdcall*)(DWORD, XINPUT_STATE*);
using T_XInputSetState = DWORD(__stdcall*)(DWORD, XINPUT_VIBRATION*);

extern "C"
{
    void __stdcall XInput_XInputEnable(BOOL enable)
    {
        if (InitRealXInput())
            reinterpret_cast<T_XInputEnable>(g_proxy[Enable])(enable);
    }

    DWORD __stdcall XInput_XInputGetBatteryInformation(
        DWORD user, BYTE devType, XINPUT_BATTERY_INFORMATION* info)
    {
        if (!InitRealXInput())
            return ERROR_DEVICE_NOT_CONNECTED;

        return reinterpret_cast<T_XInputGetBatteryInformation>(
            g_proxy[GetBatteryInformation])(user, devType, info);
    }

    DWORD __stdcall XInput_XInputGetCapabilities(
        DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps)
    {
        if (!InitRealXInput())
            return ERROR_DEVICE_NOT_CONNECTED;

        return reinterpret_cast<T_XInputGetCapabilities>(
            g_proxy[GetCapabilities])(user, flags, caps);
    }

    DWORD __stdcall XInput_XInputGetDSoundAudioDeviceGuids(
        DWORD user, GUID* renderGuid, GUID* captureGuid)
    {
        if (!InitRealXInput())
            return ERROR_DEVICE_NOT_CONNECTED;

        return reinterpret_cast<T_XInputGetDSoundAudioDeviceGuids>(
            g_proxy[GetDSoundAudioDeviceGuids])(user, renderGuid, captureGuid);
    }

    DWORD __stdcall XInput_XInputGetKeystroke(
        DWORD user, DWORD reserved, XINPUT_KEYSTROKE* key)
    {
        if (!InitRealXInput())
            return ERROR_DEVICE_NOT_CONNECTED;

        return reinterpret_cast<T_XInputGetKeystroke>(
            g_proxy[GetKeystroke])(user, reserved, key);
    }

    DWORD __stdcall XInput_XInputGetState(
        DWORD user, XINPUT_STATE* state)
    {
        if (!InitRealXInput())
            return ERROR_DEVICE_NOT_CONNECTED;

        return reinterpret_cast<T_XInputGetState>(
            g_proxy[GetState])(user, state);
    }

    DWORD __stdcall XInput_XInputSetState(
        DWORD user, XINPUT_VIBRATION* vibration)
    {
        if (!InitRealXInput())
            return ERROR_DEVICE_NOT_CONNECTED;

        return reinterpret_cast<T_XInputSetState>(
            g_proxy[SetState])(user, vibration);
    }
}

BOOL APIENTRY DllMain(
    HMODULE module,
    DWORD reason,
    LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);

        // Do not call LoadLibrary while the loader lock is held.
        // XInput exports initialize the real system DLL lazily.
        HANDLE thread = CreateThread(
            nullptr,
            0,
            WorkerThread,
            nullptr,
            0,
            nullptr);

        if (thread)
            CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        InterlockedExchange(&g_running, FALSE);

        if (g_realXInput)
        {
            FreeLibrary(g_realXInput);
            g_realXInput = nullptr;
        }
    }

    return TRUE;
}
