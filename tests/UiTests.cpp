// SPDX-License-Identifier: AGPL-3.0-or-later
// The window, driven like a user would
#include "TestHelpers.h"
#include "core/presets/PresetManager.h"
#include "ui/Displays.h"
#include "ui/MasterPanel.h"
#include "ui/StageMicEditor.h"

#include <juce_dsp/juce_dsp.h>

#include <map>

namespace stagemic::test
{

namespace
{
template <typename T>
T* findChild(juce::Component& root)
{
    if (auto* t = dynamic_cast<T*>(&root))
        return t;
    for (auto* c : root.getChildren())
        if (auto* t = findChild<T>(*c))
            return t;
    return nullptr;
}

juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged)
{
    using Source = juce::MouseInputSource;
    return {juce::Desktop::getInstance().getMainMouseSource(),
            pos,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
            Source::defaultPressure,
            Source::defaultOrientation,
            Source::defaultRotation,
            Source::defaultTiltX,
            Source::defaultTiltY,
            &c,
            &c,
            juce::Time::getCurrentTime(),
            downPos,
            juce::Time::getCurrentTime(),
            1,
            dragged};
}

// Press at `from`, drag through `via` in small steps, release
void drag(juce::Component& c, juce::Point<float> from, std::initializer_list<juce::Point<float>> via)
{
    c.mouseMove(mouseAt(c, from, from, false));
    c.mouseDown(mouseAt(c, from, from, false));
    auto at = from;
    for (const auto target : via)
        for (int step = 1; step <= 20; ++step)
        {
            const auto p = at + (target - at) * ((float)step / 20.0f);
            c.mouseDrag(mouseAt(c, p, from, true));
            if (step == 20)
                at = target;
        }
    c.mouseUp(mouseAt(c, at, from, true));
}
} // namespace

class UiTests final : public juce::UnitTest
{
public:
    UiTests()
        : juce::UnitTest("Window", "StageMic")
    {
    }

    void runTest() override
    {
        const char* eqParams[] = {pid::eqLowCut,    pid::eqLowFreq,       pid::eqLowGain,   pid::eqMidFreq,  pid::eqMidGain,
                                  pid::eqMidQ,      pid::eqLoMidFreq,     pid::eqLoMidGain, pid::eqLoMidQ,   pid::eqHiMidFreq,
                                  pid::eqHiMidGain, pid::eqHiMidQ,        pid::eqHighFreq,  pid::eqHighGain, pid::eqTameFreq,
                                  pid::eqTameRange, pid::eqTameThreshold, pid::eqPlosive};
        // Which settings each EQ point may change
        const std::vector<std::vector<const char*>> owns = {{pid::eqLowCut},
                                                            {pid::eqLowFreq, pid::eqLowGain},
                                                            {pid::eqLoMidFreq, pid::eqLoMidGain},
                                                            {pid::eqMidFreq, pid::eqMidGain},
                                                            {pid::eqHiMidFreq, pid::eqHiMidGain},
                                                            {pid::eqHighFreq, pid::eqHighGain},
                                                            {pid::eqTameFreq, pid::eqTameRange}};

        beginTest("Before / After: holding gives the untouched mic, letting go brings StageMic back, no clicks");
        {
            auto p = makeProcessor();
            p->getPresetManager().loadFactoryPreset("Karaoke Star");
            p->getChain().requestSnap();
            auto voice = sine(220.0, 0.1f, 48000 * 3);
            const auto dry = voice;
            juce::MidiBuffer midi;
            for (int start = 0; start < voice.getNumSamples(); start += 480)
            {
                const double t = start / 48000.0;
                p->setHearBefore(t >= 1.0 && t < 2.0); // held during the middle second
                juce::AudioBuffer<float> view(voice.getArrayOfWritePointers(), 2, start, 480);
                p->processBlock(view, midi);
            }
            // Held
            float worst = 0.0f;
            for (int i = 48000 + 2400; i < 96000; ++i)
                worst = std::max(worst, std::abs(voice.getSample(0, i) - dry.getSample(0, i)));
            expectLessThan(worst, 1.0e-6f, "while held: the untouched mic");
            // Let go
            float diff = 0.0f;
            for (int i = 96000 + 4800; i < 144000; ++i)
                diff = std::max(diff, std::abs(voice.getSample(0, i) - dry.getSample(0, i)));
            expectGreaterThan(diff, 0.01f, "after letting go: processed again");
            // No jumps at the switches bigger than the tone's own sample-to-sample
            float maxStep = 0.0f;
            for (int i = 1; i < voice.getNumSamples(); ++i)
                maxStep = std::max(maxStep, std::abs(voice.getSample(0, i) - voice.getSample(0, i - 1)));
            expectLessThan(maxStep, 0.2f, "fades in and out without clicks");
        }

        beginTest("Level advice: plain words for clipping, too loud, too quiet, no sound and good");
        {
            APVTS* state = nullptr;
            auto p = makeProcessor();
            state = &p->getParameters();
            MasterPanel panel(*state);
            auto feed = [&](float in, float out, int ticks) {
                for (int i = 0; i < ticks; ++i)
                    panel.setLevels(in, out);
                return panel.getAdvice();
            };
            expectEquals(feed(0.0f, 0.0f, 100), juce::String("No sound from the mic"));
            expectEquals(feed(0.02f, 0.03f, 100), juce::String("Too quiet: try Set up my voice"));
            expectEquals(feed(0.3f, 0.5f, 100), juce::String("Level is good"));
            expectEquals(feed(1.0f, 0.5f, 1), juce::String("Mic is clipping: turn its gain down"));
            expectEquals(feed(0.3f, 0.5f, 30), juce::String("Mic is clipping: turn its gain down"),
                         "an alarm stays up long enough to read");
            expectEquals(feed(0.3f, 0.5f, 60), juce::String("Level is good"), "then clears");
            expectEquals(feed(0.3f, 0.98f, 1), juce::String("Too loud: turn Output down"));
        }

        beginTest("EQ graph: dragging a point changes only that point's settings (every preset, every point)");
        for (const auto& preset : PresetManager::getFactoryPresetNames())
        {
            auto p = makeProcessor();
            p->getPresetManager().loadFactoryPreset(preset);
            p->setAdvancedMode(true);
            p->setVoiceSetupOffered();
            std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditorAndMakeActive());
            auto* eq = findChild<EqDisplay>(*editor);
            expect(eq != nullptr);
            if (eq == nullptr)
                return;

            for (int node = 0; node < EqDisplay::numNodes; ++node)
            {
                std::map<juce::String, float> before;
                for (auto* id : eqParams)
                    before[id] = getParam(*p, id);

                // Grab the point, wander over the whole graph, let go
                const auto start = eq->getNodePosition(node);
                // A preset may put two points on one spot
                bool buried = false;
                for (int other = 0; other < EqDisplay::numNodes; ++other)
                    buried = buried || (other != node && eq->getNodePosition(other).getDistanceFrom(start) < 14.0f);
                if (buried)
                    continue;
                const auto w = (float)eq->getWidth(), h = (float)eq->getHeight();
                drag(*eq, start,
                     {{w * 0.05f, h * 0.2f},
                      {w * 0.95f, h * 0.8f},
                      {w * (0.10f + 0.125f * (float)node), h * (0.22f + 0.04f * (float)node)}});

                bool ownChanged = false;
                for (auto* id : eqParams)
                {
                    const bool mine = std::find(owns[(size_t)node].begin(), owns[(size_t)node].end(), id) != owns[(size_t)node].end();
                    const bool changed = std::abs(getParam(*p, id) - before[id]) > 1.0e-4f;
                    if (mine)
                        ownChanged = ownChanged || changed;
                    else
                        expect(!changed, preset + ": dragging point " + juce::String(node + 1) + " also changed " + juce::String(id));
                }
                expect(ownChanged, preset + ": point " + juce::String(node + 1) + " follows the mouse");
            }
        }

        beginTest("EQ graph: with two points on the same spot, you grab the one you see on top");
        {
            auto p = makeProcessor();
            p->setAdvancedMode(true);
            p->setVoiceSetupOffered();
            setParam(*p, pid::eqLowCut, 120.0f);
            setParam(*p, pid::eqLowFreq, 120.0f); // point 2 exactly on point 1
            setParam(*p, pid::eqLowGain, 0.0f);
            std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditorAndMakeActive());
            auto* eq = findChild<EqDisplay>(*editor);
            expectWithinAbsoluteError(eq->getNodePosition(0).getDistanceFrom(eq->getNodePosition(1)), 0.0f, 0.5f);

            // Never touched
            auto spot = eq->getNodePosition(1);
            drag(*eq, spot, {spot.translated(60.0f, -30.0f)});
            expectWithinAbsoluteError(getParam(*p, pid::eqLowCut), 120.0f, 0.01f, "point 1 stays");
            expectGreaterThan(getParam(*p, pid::eqLowFreq), 150.0f, "point 2 moves");

            // Put point 2 back on point 1 by dragging it there, then grab again
            drag(*eq, eq->getNodePosition(1), {eq->getNodePosition(0)});
            spot = eq->getNodePosition(0);
            drag(*eq, spot, {spot.translated(80.0f, 20.0f)});
            expectWithinAbsoluteError(getParam(*p, pid::eqLowCut), 120.0f, 0.01f, "point 1 still stays");
            expectGreaterThan(getParam(*p, pid::eqLowFreq), 150.0f, "point 2 moves again");

            // And point 1 can still be reached once point 2 is out of the way
            spot = eq->getNodePosition(0);
            drag(*eq, spot, {spot.translated(-40.0f, 0.0f)});
            expectLessThan(getParam(*p, pid::eqLowCut), 110.0f, "point 1 moves when grabbed on its own");
        }

        beginTest("EQ: switching points on and off while singing doesn't click, and all off = no EQ");
        {
            auto p = makeProcessor();
            onlyModule(*p, ModuleId::Equalizer);
            setParam(*p, pid::eqPlosive, 0.0f);
            setParam(*p, pid::eqLowGain, 6.0f);
            setParam(*p, pid::eqMidGain, -6.0f);
            setParam(*p, pid::eqHighGain, 8.0f);
            setParam(*p, pid::eqLoMidGain, 5.0f);
            setParam(*p, pid::eqHiMidGain, -5.0f);
            p->getChain().requestSnap();
            auto tone = sine(150.0, 0.2f, 48000 * 4);
            juce::MidiBuffer midi;
            for (int start = 0, block = 0; start < tone.getNumSamples(); start += 480, ++block)
            {
                if (block >= 100 && block % 5 == 0) // a switch flicked every 50 ms
                    setParam(*p, pid::eqPointOn[(block / 5) % 6], ((block / 30) % 2) ? 1.0f : 0.0f);
                juce::AudioBuffer<float> view(tone.getArrayOfWritePointers(), 2, start, 480);
                p->processBlock(view, midi);
            }
            dsp::Biquad hp1, hp2;
            hp1.c = dsp::biquad::highpass(48000.0, 5000.0, 0.54119610);
            hp2.c = dsp::biquad::highpass(48000.0, 5000.0, 1.30656296);
            double high = 0.0, all = 0.0;
            for (int i = 0; i < tone.getNumSamples(); ++i)
            {
                const float x = tone.getSample(0, i), y = hp2.process(hp1.process(x, 0), 0);
                if (i < 48000)
                    continue;
                high += (double)y * y;
                all += (double)x * x;
            }
            const double noise = 10.0 * std::log10(high / all + 1.0e-20);
            logMessage("switches flicked while singing: noise above 5 kHz " + juce::String(noise, 1) + " dB");
            expectLessThan(noise, -60.0, "no clicks");

            // Every point off
            auto q = makeProcessor();
            onlyModule(*q, ModuleId::Equalizer);
            setParam(*q, pid::eqPlosive, 0.0f);
            setParam(*q, pid::eqHighGain, 8.0f);
            for (int point = 0; point < 7; ++point)
                setParam(*q, pid::eqPointOn[point], 0.0f);
            q->getChain().requestSnap();
            auto voice = sine(220.0, 0.2f, 48000);
            const auto before = voice;
            processSignal(*q, voice);
            float worst = 0.0f;
            for (int i = 0; i < voice.getNumSamples(); ++i)
                worst = std::max(worst, std::abs(voice.getSample(0, i) - before.getSample(0, i)));
            expectEquals(worst, 0.0f, "all points off: bit-exact");
        }

        beginTest("EQ: the two extra bells at 0 dB are not in the sound at all, and work when used");
        {
            auto render = [&](float loMidGain, float loMidFreq, float hiMidGain) {
                auto p = makeProcessor();
                p->getPresetManager().loadFactoryPreset("Karaoke Star");
                setParam(*p, pid::eqLoMidGain, loMidGain);
                setParam(*p, pid::eqLoMidFreq, loMidFreq);
                setParam(*p, pid::eqLoMidQ, 3.0f);
                setParam(*p, pid::eqHiMidGain, hiMidGain);
                p->getChain().requestSnap();
                juce::AudioBuffer<float> voice(2, 48000);
                juce::Random rng(3);
                for (int i = 0; i < voice.getNumSamples(); ++i)
                {
                    const float v = 0.1f * (float)std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / 48000.0) +
                                    0.02f * (rng.nextFloat() - 0.5f);
                    voice.setSample(0, i, v);
                    voice.setSample(1, i, v);
                }
                processSignal(*p, voice);
                return voice;
            };
            const auto plain = render(0.0f, 400.0f, 0.0f), moved = render(0.0f, 1500.0f, 0.0f), used = render(-9.0f, 220.0f, 0.0f);
            bool identical = true;
            for (int i = 0; i < plain.getNumSamples(); ++i)
                identical = identical && plain.getSample(0, i) == moved.getSample(0, i);
            expect(identical, "at 0 dB, where the bell sits makes no difference: bit-identical");
            expectLessThan(used.getRMSLevel(0, 24000, 24000), plain.getRMSLevel(0, 24000, 24000) * 0.8f,
                           "a -9 dB bell on the note turns it down");
        }

        beginTest("EQ: dragging points and knobs quickly while singing doesn't click or crackle");
        {
            // A pure low note has nothing above 5 kHz
            auto run = [&](bool moving, int slope, int onlyParam = -1, float* peakOut = nullptr) {
                auto p = makeProcessor();
                onlyModule(*p, ModuleId::Equalizer);
                setParam(*p, pid::eqPlosive, 0.0f);
                setParam(*p, pid::eqTameRange, 0.0f);
                setParam(*p, pid::eqLowCutSlope, (float)slope);
                p->getChain().requestSnap();
                const char* ids[] = {pid::eqLowCut, pid::eqLowFreq,   pid::eqLowGain,   pid::eqMidFreq,   pid::eqMidGain,
                                     pid::eqMidQ,   pid::eqHighFreq,  pid::eqHighGain,  pid::eqLoMidFreq, pid::eqLoMidGain,
                                     pid::eqLoMidQ, pid::eqHiMidFreq, pid::eqHiMidGain, pid::eqHiMidQ};
                auto tone = sine(150.0, 0.2f, 48000 * 4);
                juce::MidiBuffer midi;
                for (int start = 0, block = 0; start < tone.getNumSamples(); start += 480, ++block)
                {
                    if (moving)
                        for (int k = 0; k < 14; ++k)
                        {
                            if (onlyParam >= 0 && k != onlyParam)
                                continue;
                            const double phase = block * 0.01 * (1.5 + 0.37 * k); // 1.5 .. 4 sweeps a second
                            p->getParameters().getParameter(ids[k])->setValueNotifyingHost(
                                (float)(0.5 + 0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * phase)));
                        }
                    juce::AudioBuffer<float> view(tone.getArrayOfWritePointers(), 2, start, 480);
                    p->processBlock(view, midi);
                }
                dsp::Biquad hp1, hp2;
                hp1.c = dsp::biquad::highpass(48000.0, 5000.0, 0.54119610);
                hp2.c = dsp::biquad::highpass(48000.0, 5000.0, 1.30656296);
                double high = 0.0, all = 0.0;
                float peak = 0.0f;
                for (int i = 0; i < tone.getNumSamples(); ++i)
                {
                    const float x = tone.getSample(0, i), y = hp2.process(hp1.process(x, 0), 0);
                    peak = std::max(peak, std::abs(x));
                    if (i < 48000)
                        continue; // let the note's own start settle
                    high += (double)y * y;
                    all += (double)x * x;
                }
                if (peakOut != nullptr)
                    *peakOut = peak;
                else
                    expect(std::isfinite(peak) && peak <= 1.0f,
                           "stays stable (stacked boosts may reach the output ceiling, never past it)");
                return 10.0 * std::log10(high / all + 1.0e-20);
            };
            for (int k = 0; k < 14; ++k)
            {
                const char* names[] = {"low cut",   "low shelf freq", "low shelf gain", "mid freq",     "mid gain",
                                       "mid Q",     "air freq",       "air gain",       "low mid freq", "low mid gain",
                                       "low mid Q", "high mid freq",  "high mid gain",  "high mid Q"};
                float peak = 0.0f;
                const double noise = run(true, 1, k, &peak);
                logMessage(juce::String("sweeping only ") + names[k] + ": noise " + juce::String(noise, 1) + " dB, peak " +
                           juce::String(juce::Decibels::gainToDecibels(peak), 1) + " dBFS");
            }
            for (int slope : {0, 1})
            {
                const double still = run(false, slope), moving = run(true, slope);
                logMessage(juce::String(slope ? 24 : 12) + " dB/oct: noise above 5 kHz, relative to the voice: " + juce::String(still, 1) +
                           " dB still, " + juce::String(moving, 1) + " dB while every setting is swept");
                expectLessThan(moving, -60.0, "no audible clicks while sweeping");
            }
        }

        beginTest("EQ graph: the drawn curve is what the EQ really does to the sound");
        {
            juce::Random rng(5);
            juce::dsp::FFT fft(15);
            const int size = 1 << 15;
            double worstOverall = 0.0;
            for (int round = 0; round < 40; ++round)
            {
                auto p = makeProcessor();
                onlyModule(*p, ModuleId::Equalizer);
                if (round > 0) // round 0
                {
                    for (auto* id : {pid::eqLowCut, pid::eqLowFreq, pid::eqLowGain, pid::eqMidFreq, pid::eqMidGain, pid::eqMidQ,
                                     pid::eqLoMidFreq, pid::eqLoMidGain, pid::eqLoMidQ, pid::eqHiMidFreq, pid::eqHiMidGain, pid::eqHiMidQ,
                                     pid::eqHighFreq, pid::eqHighGain, pid::eqLowCutSlope})
                        p->getParameters().getParameter(id)->setValueNotifyingHost(rng.nextFloat());
                    for (int point = 0; point < 6; ++point) // about one in three points switched off
                        setParam(*p, pid::eqPointOn[point], rng.nextFloat() < 0.33f ? 0.0f : 1.0f);
                }
                setParam(*p, pid::eqPlosive, 0.0f);   // these two react to the voice
                setParam(*p, pid::eqTameRange, 0.0f); // they aren't part of the fixed curve
                p->setAdvancedMode(true);
                p->setVoiceSetupOffered();
                p->getChain().requestSnap();

                // A quiet click, so even +30 dB of boost stays far below the output
                constexpr float level = 0.01f;
                juce::AudioBuffer<float> impulse(2, size);
                impulse.clear();
                impulse.setSample(0, 0, level);
                impulse.setSample(1, 0, level);
                processSignal(*p, impulse);
                std::vector<float> data((size_t)size * 2, 0.0f);
                std::copy(impulse.getReadPointer(0), impulse.getReadPointer(0) + size, data.begin());
                fft.performFrequencyOnlyForwardTransform(data.data());

                std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditorAndMakeActive());
                auto* eq = findChild<EqDisplay>(*editor);
                double worst = 0.0;
                float worstFreq = 0.0f;
                for (float f = 30.0f; f < 16000.0f; f *= 1.06f)
                {
                    const int bin = juce::roundToInt(f * size / 48000.0f);
                    const float real = juce::Decibels::gainToDecibels(data[(size_t)bin] / level, -200.0f);
                    const float drawn = eq->getDrawnResponseDb(bin * 48000.0f / size);
                    if (real < -24.0f && drawn < -24.0f)
                        continue; // both off the bottom of the graph
                    if (std::abs(real - drawn) > worst)
                    {
                        worst = std::abs(real - drawn);
                        worstFreq = f;
                    }
                }
                worstOverall = std::max(worstOverall, worst);
                expectLessThan(worst, 0.75,
                               "round " + juce::String(round) + ": drawn curve is off by " + juce::String(worst, 1) + " dB at " +
                                   juce::String(worstFreq, 0) + " Hz (slope " +
                                   juce::String(getParam(*p, pid::eqLowCutSlope) >= 0.5f ? 24 : 12) + " dB/oct)");
            }
            logMessage("largest difference between the drawn and the real curve: " + juce::String(worstOverall, 2) + " dB");
        }
    }
};

static UiTests uiTests;

} // namespace stagemic::test
