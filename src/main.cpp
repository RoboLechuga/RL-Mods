#define WIN32_LEAN_AND_MEAN
#include "AsioPassthrough.hpp"
#include "TuningControl.hpp"
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

    std::uintptr_t ResolveEnumerationFlags()
    {
        HMODULE gameModule = GetModuleHandleW(nullptr);

        if (!gameModule)
            return 0;

        const std::uintptr_t base =
            reinterpret_cast<std::uintptr_t>(gameModule);

        std::uintptr_t addr = base + 0x00F74E90;

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

        if (!IsWritableAddress(flag1))
            return;

        if (!IsWritableAddress(flag2))
            return;

        *reinterpret_cast<volatile BYTE*>(flag1) = 1;
        *reinterpret_cast<volatile BYTE*>(flag2) = 1;
    }

    void ForceSteamScreenshot()
    {
        INPUT inputs[2] = {};

        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_F12;

        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = VK_F12;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(2, inputs, sizeof(INPUT));
    }

    DWORD WINAPI WorkerThread(void*)
    {
        AsioPassthrough::Install();
        TuningControl::Initialize();
        ScreenshotControl::Initialize();

        while (InterlockedCompareExchange(&g_running, TRUE, TRUE))
        {
            if (GetAsyncKeyState(VK_F8) & 1)
                ForceEnumeration();

            // Diagnostic: direct screenshot test outside ScreenshotControl.
            if (GetAsyncKeyState(VK_F10) & 1)
                ForceSteamScreenshot();

            ScreenshotControl::Poll();
            TuningControl::Poll();

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
        DWORD user,
        BYTE devType,
        XINPUT_BATTERY_INFORMATION* info)
    {
        if (!InitRealXInput())
            return ERROR_DEVICE_NOT_CONNECTED;

        return reinterpret_cast<T_XInputGetBatteryInformation>(
            g_proxy[GetBatteryInformation])(user, devType, info);
    }

    DWORD __stdcall XInput_XInputGetCapabilities(
        DWORD user,
        DWORD flags,
        XINPUT_CAPABILITIES* caps)
    {
        if (!InitRealXInput())
            return ERROR_DEVICE_NOT_CONNECTED;

        return reinterpret_cast<T_XInputGetCapabilities>(
            g_proxy[GetCapabilities])(user, flags, caps);
    }

    DWORD __stdcall XInput_XInputGetDSoundAudioDeviceGuids(
        DWORD user,
        GUID* renderGuid,
        GUID* captureGuid)
    {
        if (!InitRealXInput())
            return ERROR_DEVICE_NOT_CONNECTED;

        return reinterpret_cast<T_XInputGetDSoundAudioDeviceGuids>(
            g_proxy[GetDSoundAudioDeviceGuids])(user, renderGuid, captureGuid);
    }

    DWORD __stdcall XInput_XInputGetKeystroke(
        DWORD user,
        DWORD reserved,
        XINPUT_KEYSTROKE* key)
    {
        if (!InitRealXInput())
            return ERROR_DEVICE_NOT_CONNECTED;

        return reinterpret_cast<T_XInputGetKeystroke>(
            g_proxy[GetKeystroke])(user, reserved, key);
    }

    DWORD __stdcall XInput_XInputGetState(
        DWORD user,
        XINPUT_STATE* state)
    {
        if (!InitRealXInput())
            return ERROR_DEVICE_NOT_CONNECTED;

        return reinterpret_cast<T_XInputGetState>(
            g_proxy[GetState])(user, state);
    }

    DWORD __stdcall XInput_XInputSetState(
        DWORD user,
        XINPUT_VIBRATION* vibration)
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

        InitRealXInput();

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
