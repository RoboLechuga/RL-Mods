#pragma once
#include "CaptureFormat.h"

namespace Audio
{
    class IInputProcessor
    {
    public:
        virtual ~IInputProcessor() = default;
        virtual void Prepare(const CaptureFormat& format) = 0;
        virtual void Process(float* samples, std::uint32_t frameCount) = 0;
        virtual std::uint32_t GetLatencyFrames() const = 0;
    };
}
