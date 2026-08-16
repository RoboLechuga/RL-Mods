#define WIN32_LEAN_AND_MEAN
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

    std::uint32_t* g_enumFlags = nullptr;
    std::uint32_t g_hookBack = 0;

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
            if (!p) return false;

        return true;
    }

    bool GetTextSection(std::uint8_t*& start, std::size_t& length)
    {
        HMODULE module = GetModuleHandleW(nullptr);
        if (!module) return false;

        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
            reinterpret_cast<std::uint8_t*>(module) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

        auto* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        {
            if (std::memcmp(section->Name, ".text", 5) == 0)
            {
                start = reinterpret_cast<std::uint8_t*>(module) + section->VirtualAddress;
                length = section->Misc.VirtualSize;
                return true;
            }
        }
        return false;
    }

    bool PatternMatches(const std::uint8_t* data, const std::uint8_t* pattern, const char* mask)
    {
        for (; *mask; ++mask, ++data, ++pattern)
            if (*mask == 'x' && *data != *pattern) return false;
        return true;
    }

    std::uint8_t* FindPattern(std::uint8_t* start, std::size_t length,
        const std::uint8_t* pattern, const char* mask)
    {
        const std::size_t maskLen = std::strlen(mask);
        if (length < maskLen) return nullptr;

        for (std::size_t i = 0; i <= length - maskLen; ++i)
            if (PatternMatches(start + i, pattern, mask)) return start + i;

        return nullptr;
    }

    bool PlaceJumpHook(void* hookSpot, void* hookFunction, std::size_t len)
    {
        if (len < 5) return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(hookSpot, len, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        std::memset(hookSpot, 0x90, len);

        const std::uint32_t relative = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(hookFunction) -
            reinterpret_cast<std::uintptr_t>(hookSpot) - 5);

        auto* bytes = reinterpret_cast<std::uint8_t*>(hookSpot);
        bytes[0] = 0xE9;
        *reinterpret_cast<std::uint32_t*>(bytes + 1) = relative;

        FlushInstructionCache(GetCurrentProcess(), hookSpot, len);

        DWORD ignored = 0;
        VirtualProtect(hookSpot, len, oldProtect, &ignored);
        return true;
    }

    void __cdecl SaveEnumerationPointer(std::uint32_t esiValue)
    {
        if (!g_enumFlags)
            g_enumFlags = reinterpret_cast<std::uint32_t*>(esiValue + 4);
    }

    // Reproduces the working RSMods enumeration hook body. Win32/x86 only.
    void __declspec(naked) HookEnumerationService()
    {
        __asm
        {
            push ebp
            mov ebp, esp
            and esp, 0FFFFFFF8h

            pushad

            mov eax, esi
            push eax
            call SaveEnumerationPointer
            add esp, 4

            popad

            jmp dword ptr[g_hookBack]
        }
    }

    bool InstallEnumerationHook()
    {
        std::uint8_t* textStart = nullptr;
        std::size_t textLength = 0;
        if (!GetTextSection(textStart, textLength)) return false;

        static const std::uint8_t sig[] =
        {
            0x55,0x8B,0xEC,0x6A,0x00,0x68,0x00,0x00,0x00,0x00,
            0x64,0xA1,0x00,0x00,0x00,0x00,0x50,0x83,0xEC,0x00,
            0xA1,0x00,0x00,0x00,0x00,0x33,0xC5,0x89,0x45,0x00,
            0x53,0x57,0x50,0x8D,0x45,0x00,0x64,0xA3,0x00,0x00,
            0x00,0x00,0x33,0xDB,0x38,0x5E,0x00,0x0F,0x84
        };

        static const char mask[] =
            "xxxx?x????xx????xxx?x????xxxx?xxxxx?xx????xxxx?xx";

        auto* hookAddr = FindPattern(textStart, textLength, sig, mask);
        if (!hookAddr) return false;

        constexpr std::size_t hookLength = 5;
        g_hookBack = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(hookAddr) + hookLength);

        return PlaceJumpHook(hookAddr, HookEnumerationService, hookLength);
    }

    void ForceEnumeration()
    {
        if (!g_enumFlags) return;

        // Same two flags RSMods sets: BYTE at +0 and BYTE at +4.
        *reinterpret_cast<volatile BYTE*>(g_enumFlags) = 1;
        *reinterpret_cast<volatile BYTE*>(g_enumFlags + 1) = 1;
    }

    DWORD WINAPI WorkerThread(void*)
    {
        Sleep(250);

        for (int i = 0; i < 40 && InterlockedCompareExchange(&g_running, TRUE, TRUE); ++i)
        {
            if (InstallEnumerationHook()) break;
            Sleep(250);
        }

        // F8 = force song re-enumeration.
        while (InterlockedCompareExchange(&g_running, TRUE, TRUE))
        {
            if (GetAsyncKeyState(VK_F8) & 1)
                ForceEnumeration();
            Sleep(25);
        }

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
        if (InitRealXInput()) reinterpret_cast<T_XInputEnable>(g_proxy[Enable])(enable);
    }

    DWORD __stdcall XInput_XInputGetBatteryInformation(DWORD user, BYTE devType, XINPUT_BATTERY_INFORMATION* info)
    {
        if (!InitRealXInput()) return ERROR_DEVICE_NOT_CONNECTED;
        return reinterpret_cast<T_XInputGetBatteryInformation>(g_proxy[GetBatteryInformation])(user, devType, info);
    }

    DWORD __stdcall XInput_XInputGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps)
    {
        if (!InitRealXInput()) return ERROR_DEVICE_NOT_CONNECTED;
        return reinterpret_cast<T_XInputGetCapabilities>(g_proxy[GetCapabilities])(user, flags, caps);
    }

    DWORD __stdcall XInput_XInputGetDSoundAudioDeviceGuids(DWORD user, GUID* renderGuid, GUID* captureGuid)
    {
        if (!InitRealXInput()) return ERROR_DEVICE_NOT_CONNECTED;
        return reinterpret_cast<T_XInputGetDSoundAudioDeviceGuids>(g_proxy[GetDSoundAudioDeviceGuids])(user, renderGuid, captureGuid);
    }

    DWORD __stdcall XInput_XInputGetKeystroke(DWORD user, DWORD reserved, XINPUT_KEYSTROKE* key)
    {
        if (!InitRealXInput()) return ERROR_DEVICE_NOT_CONNECTED;
        return reinterpret_cast<T_XInputGetKeystroke>(g_proxy[GetKeystroke])(user, reserved, key);
    }

    DWORD __stdcall XInput_XInputGetState(DWORD user, XINPUT_STATE* state)
    {
        if (!InitRealXInput()) return ERROR_DEVICE_NOT_CONNECTED;
        return reinterpret_cast<T_XInputGetState>(g_proxy[GetState])(user, state);
    }

    DWORD __stdcall XInput_XInputSetState(DWORD user, XINPUT_VIBRATION* vibration)
    {
        if (!InitRealXInput()) return ERROR_DEVICE_NOT_CONNECTED;
        return reinterpret_cast<T_XInputSetState>(g_proxy[SetState])(user, vibration);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
if (reason == DLL_PROCESS_ATTACH)
{
    DisableThreadLibraryCalls(module);
    InitRealXInput();
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
