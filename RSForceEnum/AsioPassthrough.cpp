#include <Windows.h>
#include <objbase.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "AsioPassthrough.hpp"
#include "DelayLinePitchShifter.hpp"

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Advapi32.lib")

namespace AsioPassthrough
{
    namespace
    {
        typedef long ASIOBool;
        typedef long ASIOError;
        typedef double ASIOSampleRate;

        constexpr ASIOError ASE_OK = 0;
        constexpr long ASIOSTInt32LSB = 18;
        constexpr long MAX_ASIO_CHANNEL_NAME = 32;
        constexpr long MAX_BUFFER_FRAMES = 4096;
        constexpr float INT32_TO_FLOAT = 1.0f / 2147483648.0f;
        constexpr float FLOAT_TO_INT32 = 2147483647.0f;

        struct ASIOBufferInfo
        {
            ASIOBool isInput;
            long channelNum;
            void* buffers[2];
        };

        struct ASIOChannelInfo
        {
            long channel;
            ASIOBool isInput;
            ASIOBool isActive;
            long channelGroup;
            long type;
            char name[MAX_ASIO_CHANNEL_NAME];
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

        typedef ASIOError(__fastcall* GetChannelInfo_t)(
            void*, void*, ASIOChannelInfo*);

        typedef ASIOError(__fastcall* GetSampleRate_t)(
            void*, void*, ASIOSampleRate*);

        typedef HRESULT(STDMETHODCALLTYPE* CreateInstance_t)(
            IClassFactory*, IUnknown*, REFIID, void**);

        typedef HRESULT(STDAPICALLTYPE* DllGetClassObject_t)(
            REFCLSID, REFIID, LPVOID*);

        constexpr size_t SLOT_ASIO_GET_SAMPLE_RATE = 13;
        constexpr size_t SLOT_ASIO_GET_CHANNEL_INFO = 18;
        constexpr size_t SLOT_ASIO_CREATE_BUFFERS = 19;
        constexpr size_t SLOT_CLASS_FACTORY_CREATE_INSTANCE = 3;

        CreateInstance_t originalCreateInstance = nullptr;
        CreateBuffers_t originalCreateBuffers = nullptr;

        ASIOCallbacks originalCallbacks{};
        ASIOCallbacks hookedCallbacks{};

        void* inputBuffers[2][2] = {};
        int discoveredInputChannels = 0;
        long activeBufferFrames = 0;
        long activeSampleType = -1;
        int selectedInputChannel = 0;

        Audio::CaptureFormat format{};
        std::vector<float> conversionBuffer;

        Audio::DelayLinePitchShifter shifter(0);

        std::atomic<bool> installed{ false };
        std::atomic<bool> processingReady{ false };

        void* PatchVTableSlot(void* object, size_t slotIndex, void* replacement)
        {
            if (!object) return nullptr;

            void** vtable = *reinterpret_cast<void***>(object);
            if (!vtable) return nullptr;

            void** slot = &vtable[slotIndex];

            DWORD oldProtect = 0;
            if (!VirtualProtect(
                    slot,
                    sizeof(void*),
                    PAGE_EXECUTE_READWRITE,
                    &oldProtect))
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

        int ReadInputChannel()
        {
            return static_cast<int>(
                GetPrivateProfileIntA(
                    "Asio.Input.0",
                    "Channel",
                    0,
                    ".\\RS_ASIO.ini"));
        }

        bool ReadDriverClassId(const std::string& name, CLSID& clsid)
        {
            std::string keyPath = "SOFTWARE\\ASIO\\" + name;

            HKEY key = nullptr;
            if (RegOpenKeyExA(
                    HKEY_LOCAL_MACHINE,
                    keyPath.c_str(),
                    0,
                    KEY_READ,
                    &key) != ERROR_SUCCESS)
                return false;

            char value[128] = {};
            DWORD size = sizeof(value);
            DWORD type = 0;

            LONG result = RegQueryValueExA(
                key,
                "CLSID",
                nullptr,
                &type,
                reinterpret_cast<BYTE*>(value),
                &size);

            RegCloseKey(key);

            if (result != ERROR_SUCCESS || type != REG_SZ)
                return false;

            wchar_t wideValue[128] = {};

            MultiByteToWideChar(
                CP_ACP,
                0,
                value,
                -1,
                wideValue,
                ARRAYSIZE(wideValue));

            return SUCCEEDED(
                CLSIDFromString(wideValue, &clsid));
        }

        std::wstring ReadDriverModulePath(const CLSID& clsid)
        {
            wchar_t clsidText[128] = {};

            if (!StringFromGUID2(
                    clsid,
                    clsidText,
                    ARRAYSIZE(clsidText)))
                return {};

            std::wstring keyPath = L"CLSID\\";
            keyPath += clsidText;
            keyPath += L"\\InprocServer32";

            HKEY key = nullptr;

            if (RegOpenKeyExW(
                    HKEY_CLASSES_ROOT,
                    keyPath.c_str(),
                    0,
                    KEY_READ,
                    &key) != ERROR_SUCCESS)
                return {};

            wchar_t modulePath[MAX_PATH] = {};
            DWORD size = sizeof(modulePath);
            DWORD type = 0;

            LONG result = RegQueryValueExW(
                key,
                nullptr,
                nullptr,
                &type,
                reinterpret_cast<BYTE*>(modulePath),
                &size);

            RegCloseKey(key);

            if (result != ERROR_SUCCESS)
                return {};

            if (type != REG_SZ && type != REG_EXPAND_SZ)
                return {};

            return modulePath;
        }

        void ProcessInputBuffer(long doubleBufferIndex)
        {
            if (!processingReady.load(std::memory_order_acquire))
                return;

            if (selectedInputChannel < 0 ||
                selectedInputChannel >= discoveredInputChannels)
                return;

            if (activeSampleType != ASIOSTInt32LSB)
                return;

            if (activeBufferFrames <= 0 ||
                activeBufferFrames > MAX_BUFFER_FRAMES)
                return;

            auto* samples = reinterpret_cast<std::int32_t*>(
                inputBuffers[selectedInputChannel][doubleBufferIndex]);

            if (!samples)
                return;

            const size_t count =
                static_cast<size_t>(activeBufferFrames);

            for (size_t i = 0; i < count; ++i)
                conversionBuffer[i] =
                    static_cast<float>(samples[i]) * INT32_TO_FLOAT;

            shifter.Process(
                conversionBuffer.data(),
                static_cast<std::uint32_t>(activeBufferFrames));

            for (size_t i = 0; i < count; ++i)
            {
                float value = conversionBuffer[i];

                if (value < -1.0f) value = -1.0f;
                if (value > 1.0f) value = 1.0f;

                samples[i] =
                    static_cast<std::int32_t>(value * FLOAT_TO_INT32);
            }
        }

        void HookBufferSwitch(long index, ASIOBool directProcess)
        {
            ProcessInputBuffer(index);

            if (originalCallbacks.bufferSwitch)
                originalCallbacks.bufferSwitch(index, directProcess);
        }

        ASIOTime* HookBufferSwitchTimeInfo(
            ASIOTime* params,
            long index,
            ASIOBool directProcess)
        {
            ProcessInputBuffer(index);

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

            const ASIOError result =
                originalCreateBuffers(
                    self,
                    unused,
                    infos,
                    numChannels,
                    bufferSize,
                    callbacks ? &hookedCallbacks : nullptr);

            if (result != ASE_OK)
                return result;

            processingReady.store(false, std::memory_order_release);

            activeBufferFrames = bufferSize;
            discoveredInputChannels = 0;
            activeSampleType = -1;

            auto getChannelInfo =
                reinterpret_cast<GetChannelInfo_t>(
                    (*reinterpret_cast<void***>(self))
                        [SLOT_ASIO_GET_CHANNEL_INFO]);

            for (long i = 0;
                 i < numChannels && discoveredInputChannels < 2;
                 ++i)
            {
                if (!infos[i].isInput)
                    continue;

                const int index = discoveredInputChannels++;

                inputBuffers[index][0] = infos[i].buffers[0];
                inputBuffers[index][1] = infos[i].buffers[1];

                ASIOChannelInfo channelInfo{};
                channelInfo.channel = infos[i].channelNum;
                channelInfo.isInput = 1;

                if (getChannelInfo &&
                    getChannelInfo(
                        self,
                        nullptr,
                        &channelInfo) == ASE_OK)
                {
                    if (index == selectedInputChannel)
                        activeSampleType = channelInfo.type;
                }
            }

            ASIOSampleRate sampleRate = 0.0;

            auto getSampleRate =
                reinterpret_cast<GetSampleRate_t>(
                    (*reinterpret_cast<void***>(self))
                        [SLOT_ASIO_GET_SAMPLE_RATE]);

            if (getSampleRate &&
                getSampleRate(
                    self,
                    nullptr,
                    &sampleRate) == ASE_OK &&
                sampleRate > 0.0)
            {
                format.sampleRate =
                    static_cast<std::uint32_t>(sampleRate);
            }
            else
            {
                format.sampleRate = 0;
            }

            format.sampleFormat =
                activeSampleType == ASIOSTInt32LSB
                    ? Audio::SampleFormat::Int32
                    : Audio::SampleFormat::Unsupported;

            format.channelCount = 1;

            if (format.IsUsable() &&
                selectedInputChannel >= 0 &&
                selectedInputChannel < discoveredInputChannels &&
                bufferSize > 0 &&
                bufferSize <= MAX_BUFFER_FRAMES)
            {
                // All allocations happen here, before the realtime callback.
                conversionBuffer.assign(
                    static_cast<size_t>(MAX_BUFFER_FRAMES),
                    0.0f);

                shifter.Prepare(format);

                processingReady.store(
                    true,
                    std::memory_order_release);
            }

            return result;
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
                originalCreateInstance(
                    self,
                    outer,
                    riid,
                    created);

            if (FAILED(result) || !created || !*created)
                return result;

            if (!originalCreateBuffers)
            {
                originalCreateBuffers =
                    reinterpret_cast<CreateBuffers_t>(
                        PatchVTableSlot(
                            *created,
                            SLOT_ASIO_CREATE_BUFFERS,
                            reinterpret_cast<void*>(
                                HookCreateBuffers)));
            }

            return result;
        }
    }

    bool Install()
    {
        if (installed.load())
            return true;

        selectedInputChannel = ReadInputChannel();

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
                GetProcAddress(
                    driverModule,
                    "DllGetClassObject"));

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
                    reinterpret_cast<void*>(
                        HookCreateInstance)));

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
