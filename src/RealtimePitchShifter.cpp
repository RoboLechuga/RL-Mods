/*
    RSMods-Min realtime pitch shifter

    DSP core adapted from Stephan M. Bernsee's smbPitchShift algorithm,
    version 1.2. It has been refactored here from process-global static state
    into a per-instance C++ class so two Rocksmith players can be processed
    independently, with a true dry bypass at E Standard / A440.

    Original work:
    Copyright 1999-2009 Stephan M. Bernsee
    https://blogs.zynaptiq.com/bernsee/

    The Wide Open License (WOL)

    Permission to use, copy, modify, distribute and sell this software and its
    documentation for any purpose is hereby granted without fee, provided that
    the above copyright notice and this license appear in all source copies.
    THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT EXPRESS OR IMPLIED WARRANTY OF
    ANY KIND. See http://www.dspguru.com/wol.htm for more information.
*/

#include "RealtimePitchShifter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Audio
{
    namespace
    {
        constexpr float PI = 3.14159265358979323846f;
        constexpr int DEFAULT_REFERENCE_HZ = 440;

        constexpr float RATIO_SLEW_MS = 10.0f;
        constexpr float BYPASS_RAMP_MS = 6.0f;

        constexpr float MIN_RATIO = 0.45f;
        constexpr float MAX_RATIO = 2.05f;
    }

    RealtimePitchShifter::RealtimePitchShifter(int semitones)
    {
        SetSemitones(semitones);
    }

    float RealtimePitchShifter::RatioForTuning(
        int semitones,
        int referenceHz)
    {
        if (referenceHz < 1)
            referenceHz = DEFAULT_REFERENCE_HZ;

        const float coarse =
            std::pow(
                2.0f,
                static_cast<float>(semitones) / 12.0f);

        const float reference =
            static_cast<float>(referenceHz) /
            static_cast<float>(DEFAULT_REFERENCE_HZ);

        return std::clamp(
            coarse * reference,
            MIN_RATIO,
            MAX_RATIO);
    }

    void RealtimePitchShifter::SetSemitones(int semitones)
    {
        SetTuning(semitones, DEFAULT_REFERENCE_HZ);
    }

    void RealtimePitchShifter::SetTuning(
        int semitones,
        int referenceHz)
    {
        if (referenceHz < 1)
            referenceHz = DEFAULT_REFERENCE_HZ;

        targetRatio.store(
            RatioForTuning(semitones, referenceHz),
            std::memory_order_relaxed);

        neutral.store(
            semitones == 0 &&
            referenceHz == DEFAULT_REFERENCE_HZ,
            std::memory_order_relaxed);
    }

    bool RealtimePitchShifter::IsNeutral() const
    {
        return neutral.load(std::memory_order_relaxed);
    }

    void RealtimePitchShifter::ResetState()
    {
        inputFifo.fill(0.0f);
        outputFifo.fill(0.0f);
        fftWorkspace.fill(0.0f);
        lastPhase.fill(0.0f);
        sumPhase.fill(0.0f);
        outputAccum.fill(0.0f);
        analysisFrequency.fill(0.0f);
        analysisMagnitude.fill(0.0f);
        synthesisFrequency.fill(0.0f);
        synthesisMagnitude.fill(0.0f);

        rover = FIFO_LATENCY;
    }

    void RealtimePitchShifter::Prepare(
        const CaptureFormat& format)
    {
        sampleRate =
            format.sampleRate > 0
                ? static_cast<float>(format.sampleRate)
                : 48000.0f;

        ResetState();

        currentRatio =
            targetRatio.load(
                std::memory_order_relaxed);

        ratioStep =
            1.0f /
            std::max(
                1.0f,
                sampleRate *
                RATIO_SLEW_MS *
                0.001f);

        wetStep =
            1.0f /
            std::max(
                1.0f,
                sampleRate *
                BYPASS_RAMP_MS *
                0.001f);

        wetMix =
            IsNeutral()
                ? 0.0f
                : 1.0f;
    }

    void RealtimePitchShifter::Fft(
        float* fftBuffer,
        long frameSize,
        long sign)
    {
        float wr;
        float wi;
        float arg;
        float temp;
        float tr;
        float ti;
        float ur;
        float ui;

        long i;
        long bitm;
        long j;
        long le = 2;
        long le2;

        for (i = 2;
             i < 2 * frameSize - 2;
             i += 2)
        {
            for (bitm = 2, j = 0;
                 bitm < 2 * frameSize;
                 bitm <<= 1)
            {
                if (i & bitm)
                    ++j;

                j <<= 1;
            }

            if (i < j)
            {
                temp = fftBuffer[i];
                fftBuffer[i] = fftBuffer[j];
                fftBuffer[j] = temp;

                temp = fftBuffer[i + 1];
                fftBuffer[i + 1] = fftBuffer[j + 1];
                fftBuffer[j + 1] = temp;
            }
        }

        long stages = 0;
        for (long n = frameSize; n > 1; n >>= 1)
            ++stages;

        for (long stage = 0;
             stage < stages;
             ++stage)
        {
            le <<= 1;
            le2 = le >> 1;

            ur = 1.0f;
            ui = 0.0f;

            arg =
                PI /
                static_cast<float>(
                    le2 >> 1);

            wr = std::cos(arg);
            wi =
                static_cast<float>(sign) *
                std::sin(arg);

            for (j = 0;
                 j < le2;
                 j += 2)
            {
                for (i = j;
                     i < 2 * frameSize;
                     i += le)
                {
                    const long p1 = i;
                    const long p2 = p1 + le2;

                    tr =
                        fftBuffer[p2] * ur -
                        fftBuffer[p2 + 1] * ui;

                    ti =
                        fftBuffer[p2] * ui +
                        fftBuffer[p2 + 1] * ur;

                    fftBuffer[p2] =
                        fftBuffer[p1] - tr;

                    fftBuffer[p2 + 1] =
                        fftBuffer[p1 + 1] - ti;

                    fftBuffer[p1] += tr;
                    fftBuffer[p1 + 1] += ti;
                }

                tr =
                    ur * wr -
                    ui * wi;

                ui =
                    ur * wi +
                    ui * wr;

                ur = tr;
            }
        }
    }

    float RealtimePitchShifter::ProcessOne(
        float input,
        float pitchRatio)
    {
        const float freqPerBin =
            sampleRate /
            static_cast<float>(
                FFT_SIZE);

        const float expectedPhase =
            2.0f *
            PI *
            static_cast<float>(
                STEP_SIZE) /
            static_cast<float>(
                FFT_SIZE);

        inputFifo[rover] = input;

        const float output =
            outputFifo[
                rover -
                FIFO_LATENCY];

        ++rover;

        if (rover < FFT_SIZE)
            return output;

        rover = FIFO_LATENCY;

        for (long k = 0;
             k < FFT_SIZE;
             ++k)
        {
            const float window =
                -0.5f *
                    std::cos(
                        2.0f *
                        PI *
                        static_cast<float>(k) /
                        static_cast<float>(
                            FFT_SIZE)) +
                0.5f;

            fftWorkspace[2 * k] =
                inputFifo[k] *
                window;

            fftWorkspace[
                2 * k + 1] =
                0.0f;
        }

        Fft(
            fftWorkspace.data(),
            FFT_SIZE,
            -1);

        for (long k = 0;
             k <= HALF_FFT;
             ++k)
        {
            const float real =
                fftWorkspace[2 * k];

            const float imag =
                fftWorkspace[
                    2 * k + 1];

            const float magnitude =
                2.0f *
                std::sqrt(
                    real * real +
                    imag * imag);

            const float phase =
                std::atan2(
                    imag,
                    real);

            float delta =
                phase -
                lastPhase[k];

            lastPhase[k] =
                phase;

            delta -=
                static_cast<float>(k) *
                expectedPhase;

            long quadrant =
                static_cast<long>(
                    delta /
                    PI);

            if (quadrant >= 0)
                quadrant +=
                    quadrant & 1;
            else
                quadrant -=
                    quadrant & 1;

            delta -=
                PI *
                static_cast<float>(
                    quadrant);

            delta =
                static_cast<float>(
                    OVERSAMPLE) *
                delta /
                (2.0f * PI);

            analysisMagnitude[k] =
                magnitude;

            analysisFrequency[k] =
                static_cast<float>(k) *
                    freqPerBin +
                delta *
                    freqPerBin;
        }

        synthesisMagnitude.fill(0.0f);
        synthesisFrequency.fill(0.0f);

        for (long k = 0;
             k <= HALF_FFT;
             ++k)
        {
            const long index =
                static_cast<long>(
                    static_cast<float>(k) *
                    pitchRatio);

            if (index <= HALF_FFT)
            {
                synthesisMagnitude[index] +=
                    analysisMagnitude[k];

                synthesisFrequency[index] =
                    analysisFrequency[k] *
                    pitchRatio;
            }
        }

        for (long k = 0;
             k <= HALF_FFT;
             ++k)
        {
            const float magnitude =
                synthesisMagnitude[k];

            float frequency =
                synthesisFrequency[k];

            frequency -=
                static_cast<float>(k) *
                freqPerBin;

            frequency /=
                freqPerBin;

            frequency =
                2.0f *
                PI *
                frequency /
                static_cast<float>(
                    OVERSAMPLE);

            frequency +=
                static_cast<float>(k) *
                expectedPhase;

            sumPhase[k] = std::remainder(
                sumPhase[k] + frequency,
                2.0f * PI);

            const float phase =
                sumPhase[k];

            fftWorkspace[2 * k] =
                magnitude *
                std::cos(phase);

            fftWorkspace[
                2 * k + 1] =
                magnitude *
                std::sin(phase);
        }

        for (long k =
                 FFT_SIZE + 2;
             k <
                 2 * FFT_SIZE;
             ++k)
        {
            fftWorkspace[k] =
                0.0f;
        }

        Fft(
            fftWorkspace.data(),
            FFT_SIZE,
            1);

        for (long k = 0;
             k < FFT_SIZE;
             ++k)
        {
            const float window =
                -0.5f *
                    std::cos(
                        2.0f *
                        PI *
                        static_cast<float>(k) /
                        static_cast<float>(
                            FFT_SIZE)) +
                0.5f;

            outputAccum[k] +=
                2.0f *
                window *
                fftWorkspace[2 * k] /
                (static_cast<float>(
                     HALF_FFT) *
                 static_cast<float>(
                     OVERSAMPLE));
        }

        for (long k = 0;
             k < STEP_SIZE;
             ++k)
        {
            outputFifo[k] =
                outputAccum[k];
        }

        std::memmove(
            outputAccum.data(),
            outputAccum.data() +
                STEP_SIZE,
            FFT_SIZE *
                sizeof(float));

        for (long k = 0;
             k < FIFO_LATENCY;
             ++k)
        {
            inputFifo[k] =
                inputFifo[
                    k + STEP_SIZE];
        }

        return output;
    }

    void RealtimePitchShifter::Process(
        float* samples,
        std::uint32_t frameCount)
    {
        if (!samples)
            return;

        const float target =
            targetRatio.load(
                std::memory_order_relaxed);

        const bool nowNeutral =
            neutral.load(
                std::memory_order_relaxed);

        for (std::uint32_t i = 0;
             i < frameCount;
             ++i)
        {
            const float dry =
                samples[i];

            const float difference =
                target -
                currentRatio;

            if (std::fabs(difference) <=
                ratioStep)
            {
                currentRatio = target;
            }
            else
            {
                currentRatio +=
                    difference > 0.0f
                        ? ratioStep
                        : -ratioStep;
            }

            const float wet =
                ProcessOne(
                    dry,
                    currentRatio);

            const float targetWet =
                nowNeutral
                    ? 0.0f
                    : 1.0f;

            if (wetMix < targetWet)
            {
                wetMix =
                    std::min(
                        targetWet,
                        wetMix +
                        wetStep);
            }
            else if (wetMix > targetWet)
            {
                wetMix =
                    std::max(
                        targetWet,
                        wetMix -
                        wetStep);
            }

            if (nowNeutral)
            {
                // True neutral path: never let the DSP result participate in
                // the audible sample at E Standard / A440. The shifter still
                // runs above so its internal state remains warm.
                samples[i] = dry;
            }
            else if (std::isfinite(wet))
            {
                samples[i] =
                    dry +
                    (wet - dry) *
                        wetMix;
            }
            else
            {
                // Fail safe: a bad DSP value must never be written back to
                // the ASIO input buffer.
                samples[i] = dry;
            }
        }
    }

    std::uint32_t
    RealtimePitchShifter::GetLatencyFrames() const
    {
        if (IsNeutral())
            return 0;

        return FIFO_LATENCY;
    }
}
