#pragma once
#include <cstdint>

namespace Audio
{
    enum class SampleFormat
    {
        Unsupported,
        Float32,
        Int32,
        Int16
    };

    struct CaptureFormat
    {
        SampleFormat sampleFormat = SampleFormat::Unsupported;
        std::uint32_t sampleRate = 0;
        std::uint32_t channelCount = 0;

        bool IsUsable() const
        {
            return sampleFormat != SampleFormat::Unsupported
                && sampleRate > 0
                && channelCount > 0;
        }
    };
}
