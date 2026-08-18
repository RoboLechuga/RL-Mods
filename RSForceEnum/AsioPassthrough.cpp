#include <Windows.h>
#include <objbase.h>
#include <atomic>
#include <string>
#include "AsioPassthrough.hpp"

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Advapi32.lib")

namespace AsioPassthrough
{
    namespace
    {
        typedef long ASIOBool;
        typedef long ASIOError;
        typedef double ASIOSampleRate;

        struct ASIOBufferInfo
        {
            ASIOBool isInput;
            long channelNum;
            void* buffers[2];
        };

        struct ASIOTime;

        struct ASIOCallbacks
        {
            void (*bufferSwitch)(long, ASIOBool);
            void (*sampleRateDidChange)(ASIOSampleRate);
            long (*asioMessage)(long, long, void*, double*);
            ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime*, long, ASIOBool);
        };

        typedef ASIOError(__fastcall* CreateBuffers_t)(
            void*, void*, ASIOBufferInfo*, long, long, ASIOCallbacks*);

        typedef HRESULT(STDMETHODCALLTYPE* CreateInstance_t)(
            IClassFactory*, IUnknown*, REFIID, void**);

        typedef HRESULT(STDAPICALLTYPE* DllGetClassObject_t)(
            REFCLSID, REFIID, LPVOID*);

        constexpr size_t SLOT_CLASS_FACTORY_CREATE_INSTANCE = 3;
        constexpr size_t SLOT_ASIO_CREATE_BUFFERS = 19;

        CreateInstance_t originalCreateInstance = nullptr;
        CreateBuffers_t originalCreateBuffers = nullptr;
        ASIOCallbacks originalCallbacks{};
        ASIOCallbacks hookedCallbacks{};
        std::atomic<bool> installed{ false };

        void* PatchVTableSlot(void* object, size_t slotIndex, void* replacement)
        {
            if (!object) return nullptr;

            void** vtable = *reinterpret_cast<void***>(object);
            if (!vtable) return nullptr;

            void** slot = &vtable[slotIndex];

            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
                return nullptr;

            void* original = *slot;
            *slot = replacement;

            DWORD ignored = 0;
            VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);

            return original;
        }

        std::string ReadDriverName()
        {
            char driver[512] = {};

            GetPrivateProfileStringA(
                "Asio.Input.0", "Driver", "",
                driver, sizeof(driver), ".\\RS_ASIO.ini");

            if (driver[0]) return driver;

            GetPrivateProfileStringA(
                "Asio.Output", "Driver", "",
                driver, sizeof(driver), ".\\RS_ASIO.ini");

            return driver;
        }

        bool ReadDriverClassId(const std::string& name, CLSID& clsid)
        {
            std::string keyPath = "SOFTWARE\\ASIO\\" + name;

            HKEY key = nullptr;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_READ, &key)
                != ERROR_SUCCESS)
                return false;

            char value[128] = {};
            DWORD size = sizeof(value);
            DWORD type = 0;

            LONG result = RegQueryValueExA(
                key, "CLSID", nullptr, &type,
                reinterpret_cast<BYTE*>(value), &size);

            RegCloseKey(key);

            if (result != ERROR_SUCCESS || type != REG_SZ)
                return false;

            wchar_t wideValue[128] = {};
            MultiByteToWideChar(
                CP_ACP, 0, value, -1,
                wideValue, ARRAYSIZE(wideValue));

            return SUCCEEDED(CLSIDFromString(wideValue, &clsid));
        }

        std::wstring ReadDriverModulePath(const CLSID& clsid)
        {
            wchar_t clsidText[128] = {};

            if (!StringFromGUID2(clsid, clsidText, ARRAYSIZE(clsidText)))
                return {};

            std::wstring keyPath = L"CLSID\\";
            keyPath += clsidText;
            keyPath += L"\\InprocServer32";

            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, KEY_READ, &key)
                != ERROR_SUCCESS)
                return {};

            wchar_t modulePath[MAX_PATH] = {};
            DWORD size = sizeof(modulePath);
            DWORD type = 0;

            LONG result = RegQueryValueExW(
                key, nullptr, nullptr, &type,
                reinterpret_cast<BYTE*>(modulePath), &size);

            RegCloseKey(key);

            if (result != ERROR_SUCCESS)
                return {};

            if (type != REG_SZ && type != REG_EXPAND_SZ)
                return {};

            return modulePath;
        }

        // Pure pass-through. No sample processing.
        void HookBufferSwitch(long index, ASIOBool directProcess)
        {
            if (originalCallbacks.bufferSwitch)
                originalCallbacks.bufferSwitch(index, directProcess);
        }

        ASIOTime* HookBufferSwitchTimeInfo(
            ASIOTime* params, long index, ASIOBool directProcess)
        {
            if (originalCallbacks.bufferSwitchTimeInfo)
                return originalCallbacks.bufferSwitchTimeInfo(
                    params, index, directProcess);

            return params;
        }

        ASIOError __fastcall HookCreateBuffers(
            void* self,
            void* unused,
            ASIOBufferInfo* infos,
            long numChannels,
            long bufferSize,
            ASIOCallbacks* callbacks)
        {
            if (!originalCreateBuffers)
                return -1;

            if (callbacks)
            {
                originalCallbacks = *callbacks;

                hookedCallbacks.bufferSwitch = HookBufferSwitch;
                hookedCallbacks.sampleRateDidChange =
                    originalCallbacks.sampleRateDidChange;
                hookedCallbacks.asioMessage =
                    originalCallbacks.asioMessage;
                hookedCallbacks.bufferSwitchTimeInfo =
                    originalCallbacks.bufferSwitchTimeInfo
                    ? HookBufferSwitchTimeInfo
                    : nullptr;
            }

            return originalCreateBuffers(
                self, unused, infos, numChannels, bufferSize,
                callbacks ? &hookedCallbacks : nullptr);
        }

        HRESULT STDMETHODCALLTYPE HookCreateInstance(
            IClassFactory* self,
            IUnknown* outer,
            REFIID riid,
            void** created)
        {
            if (!originalCreateInstance)
                return E_FAIL;

            HRESULT result =
                originalCreateInstance(self, outer, riid, created);

            if (FAILED(result) || !created || !*created)
                return result;

            if (!originalCreateBuffers)
            {
                originalCreateBuffers =
                    reinterpret_cast<CreateBuffers_t>(
                        PatchVTableSlot(
                            *created,
                            SLOT_ASIO_CREATE_BUFFERS,
                            reinterpret_cast<void*>(HookCreateBuffers)));
            }

            return result;
        }
    }

    bool Install()
    {
        if (installed.load())
            return true;

        const std::string driverName = ReadDriverName();
        if (driverName.empty())
            return false;

        CLSID driverClsid{};
        if (!ReadDriverClassId(driverName, driverClsid))
            return false;

        const std::wstring modulePath =
            ReadDriverModulePath(driverClsid);

        if (modulePath.empty())
            return false;

        HMODULE driverModule =
            LoadLibraryW(modulePath.c_str());

        if (!driverModule)
            return false;

        auto dllGetClassObject =
            reinterpret_cast<DllGetClassObject_t>(
                GetProcAddress(driverModule, "DllGetClassObject"));

        if (!dllGetClassObject)
            return false;

        IClassFactory* factory = nullptr;

        HRESULT hr =
            dllGetClassObject(
                driverClsid,
                IID_IClassFactory,
                reinterpret_cast<void**>(&factory));

        if (FAILED(hr) || !factory)
            return false;

        originalCreateInstance =
            reinterpret_cast<CreateInstance_t>(
                PatchVTableSlot(
                    factory,
                    SLOT_CLASS_FACTORY_CREATE_INSTANCE,
                    reinterpret_cast<void*>(HookCreateInstance)));

        factory->Release();

        if (!originalCreateInstance)
            return false;

        installed.store(true);
        return true;
    }

    bool IsInstalled()
    {
        return installed.load();
    }
}
