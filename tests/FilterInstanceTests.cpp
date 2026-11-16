// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AllocationGuard.h"
#include "TestHelpers.h"
#include "core/FilterInstance.h"

#include <vector>

namespace stagemic::test
{

namespace
{
// Planar audio like OBS delivers
struct PlanarAudio
{
    PlanarAudio(int channels, int frames, juce::Random& rng)
        : data((size_t)channels, std::vector<float>((size_t)frames))
    {
        for (auto& ch : data)
            for (auto& s : ch)
                s = rng.nextFloat() * 2.0f - 1.0f;
        refreshPointers();
    }

    void refreshPointers()
    {
        pointers.clear();
        for (auto& ch : data)
            pointers.push_back(ch.data());
    }

    std::vector<std::vector<float>> data;
    std::vector<float*> pointers;
};

FilterInstance::Config makeConfig(double sampleRate, int channels)
{
    FilterInstance::Config c;
    c.sampleRate = sampleRate;
    c.numHostChannels = channels;
    c.maxBlockSize = 1024;
    c.hostDescription = "unit test";
    return c;
}
} // namespace

class FilterInstanceTests final : public juce::UnitTest
{
public:
    FilterInstanceTests()
        : juce::UnitTest("FilterInstance", "StageMic")
    {
    }

    void runTest() override
    {
        juce::Random rng(1234);

        beginTest("With every module off, audio passes bit-exact (all rates, channel counts, block sizes)");
        for (double sr : {44100.0, 48000.0, 96000.0})
            for (int channels : {1, 2, 6})
            {
                FilterInstance instance(makeConfig(sr, channels));
                allModulesOff(instance.getProcessor());
                expectEquals(instance.getProcessedChannelCount(), juce::jmin(channels, 2));

                for (int frames : {1, 7, 64, 441, 480, 1024, 1025, 3000})
                {
                    PlanarAudio audio(channels, frames, rng);
                    const auto original = audio.data;
                    instance.process(audio.pointers.data(), channels, frames);
                    expect(audio.data == original, "changed audio at " + juce::String(sr) + " Hz, " + juce::String(channels) + " ch, " +
                                                       juce::String(frames) + " frames");
                }
            }

        beginTest("Audio path never allocates memory (default sound)");
        {
            FilterInstance instance(makeConfig(48000.0, 2));
            PlanarAudio audio(2, 3000, rng);
            instance.process(audio.pointers.data(), 2, 480); // warm-up

            std::size_t allocations = 0;
            {
                AllocationGuard guard;
                for (int i = 0; i < 200; ++i)
                    instance.process(audio.pointers.data(), 2, i % 2 == 0 ? 480 : 3000);
                allocations = guard.count(); // read before building any test messages
            }
            expectEquals((int)allocations, 0, "heap allocations on the audio path");
        }

        beginTest("Bad input is ignored safely");
        {
            FilterInstance instance(makeConfig(48000.0, 2));
            allModulesOff(instance.getProcessor());
            instance.process(nullptr, 2, 480);
            PlanarAudio audio(2, 480, rng);
            instance.process(audio.pointers.data(), 2, 0);
            instance.process(audio.pointers.data(), 0, 480);
            float* withNull[2] = {audio.pointers[0], nullptr};
            const auto original = audio.data;
            instance.process(withNull, 2, 480);
            expect(audio.data == original);
        }

        beginTest("Meters see the signal");
        {
            FilterInstance instance(makeConfig(48000.0, 2));
            allModulesOff(instance.getProcessor());
            std::vector<float> left(480, 0.0f), right(480, 0.0f);
            left[100] = 0.5f;
            right[200] = -0.25f;
            float* ptrs[2] = {left.data(), right.data()};
            instance.process(ptrs, 2, 480);

            auto& p = instance.getProcessor();
            expectWithinAbsoluteError(p.getInputMeter().takePeak(), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError(p.getOutputMeter().takePeak(), 0.5f, 1.0e-6f);
            expectEquals(p.getInputMeter().takePeak(), 0.0f, "meter resets after reading");
        }

        beginTest("State round-trips and bad state is rejected");
        {
            FilterInstance a(makeConfig(48000.0, 2));
            const auto state = a.getState();
            expect(!state.empty());

            FilterInstance b(makeConfig(48000.0, 2));
            expect(b.setState(state));
            expect(b.getState() == state);

            expect(!b.setState(""));
            expect(!b.setState("this is not base64 !!!"));
            expect(!b.setState(juce::Base64::toBase64("<SomethingElse/>").toStdString()));
            expect(b.getState() == state, "bad state must not change anything");
        }

        beginTest("Processor reports the configuration OBS gave it");
        {
            FilterInstance mono(makeConfig(44100.0, 1));
            expectEquals(mono.getProcessor().getTotalNumInputChannels(), 1);
            expectEquals(mono.getProcessor().getPreparedSampleRate(), 44100.0);
            expectEquals(mono.getProcessor().getPreparedBlockSize(), 1024);
            expectEquals(mono.getProcessor().getLatencySamples(), 0);

            FilterInstance surround(makeConfig(48000.0, 6));
            expectEquals(surround.getProcessor().getTotalNumInputChannels(), 2);
        }
    }
};

static FilterInstanceTests filterInstanceTests;

} // namespace stagemic::test
