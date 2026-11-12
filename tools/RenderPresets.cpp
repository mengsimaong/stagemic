// SPDX-License-Identifier: AGPL-3.0-or-later
//
// stagemic_render: run a recording through StageMic offline, once per preset,
// so presets can be judged by ear without OBS.
//
//   stagemic_render <input.wav|flac|aiff|mp3> <output-folder> [preset names or .stagemicpreset files...]
//
// A preset can carry changes after '@', applied after it loads, e.g.
//   "Hard Tune@tune_key=11,tune_scale=1"   (Hard Tune in B major)
//
// With no presets given, every factory preset is rendered. Also writes the
// untouched input ("00 Dry.wav") for comparison. Audio is processed exactly as
// in OBS: same engine, 480-sample blocks, stereo (a mono file is duplicated
// to both channels, as OBS does with a mono mic).

#include "core/StageMicProcessor.h"
#include "core/presets/PresetManager.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

namespace
{
int fail(const juce::String& message)
{
    std::fprintf(stderr, "error: %s\n", message.toRawUTF8());
    return 1;
}

bool writeWav(const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    if (!static_cast<juce::FileOutputStream*>(stream.get())->openedOk())
        return false;

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                  .withSampleRate(sampleRate)
                                                  .withNumChannels(audio.getNumChannels())
                                                  .withBitsPerSample(24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}

struct Stats
{
    float peakDb, rmsDb;
};

Stats measure(const juce::AudioBuffer<float>& b)
{
    float peak = 0.0f;
    double sum = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        peak = juce::jmax(peak, b.getMagnitude(c, 0, b.getNumSamples()));
        const float r = b.getRMSLevel(c, 0, b.getNumSamples());
        sum += (double)r * r;
    }
    const float rms = (float)std::sqrt(sum / juce::jmax(1, b.getNumChannels()));
    return {juce::Decibels::gainToDecibels(peak, -120.0f), juce::Decibels::gainToDecibels(rms, -120.0f)};
}
} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 3)
    {
        std::printf("usage: stagemic_render <input audio> <output folder> [preset names or files...]\n");
        return 1;
    }

    const juce::File input = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1]));
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[2]));
    if (!input.existsAsFile())
        return fail("input not found: " + input.getFullPathName());
    if (!outDir.createDirectory())
        return fail("cannot create " + outDir.getFullPathName());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(input));
    if (reader == nullptr)
        return fail("unsupported or unreadable audio file: " + input.getFileName());

    const double sampleRate = reader->sampleRate;
    const int length = (int)reader->lengthInSamples;
    juce::AudioBuffer<float> source(2, length);
    reader->read(&source, 0, length, 0, true, true); // mono files fill both channels

    juce::StringArray presets;
    for (int i = 3; i < argc; ++i)
        presets.add(juce::String::fromUTF8(argv[i]));
    if (presets.isEmpty())
        presets = stagemic::PresetManager::getFactoryPresetNames();

    writeWav(outDir.getChildFile("00 Dry.wav"), source, sampleRate);
    const auto dry = measure(source);
    std::printf("%-28s peak %6.1f dBFS   rms %6.1f dBFS\n", "00 Dry", dry.peakDb, dry.rmsDb);

    int index = 1;
    for (const auto& entry : presets)
    {
        const auto preset = entry.upToFirstOccurrenceOf("@", false, false);
        const auto changes = entry.fromFirstOccurrenceOf("@", false, false);
        stagemic::StageMicProcessor processor;
        processor.setRateAndBufferSizeDetails(sampleRate, 1024);
        processor.prepareToPlay(sampleRate, 1024);

        auto& manager = processor.getPresetManager();
        const juce::File presetFile = juce::File::getCurrentWorkingDirectory().getChildFile(preset);
        const bool ok = presetFile.existsAsFile() ? manager.loadPresetFile(presetFile) : manager.loadFactoryPreset(preset);
        if (!ok)
        {
            std::fprintf(stderr, "skipping unknown preset: %s\n", preset.toRawUTF8());
            continue;
        }
        for (const auto& change : juce::StringArray::fromTokens(changes, ",", ""))
        {
            const auto id = change.upToFirstOccurrenceOf("=", false, false).trim();
            auto* parameter = processor.getParameters().getParameter(id);
            if (parameter == nullptr)
                return fail("unknown setting: " + id);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(change.fromFirstOccurrenceOf("=", false, false).getFloatValue()));
        }
        processor.getChain().requestSnap();

        auto audio = source;
        juce::MidiBuffer midi;
        for (int start = 0; start < length; start += 480)
        {
            const int n = juce::jmin(480, length - start);
            juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, start, n);
            processor.processBlock(view, midi);
        }

        const auto name = juce::String(index++).paddedLeft('0', 2) + " " + processor.getCurrentPresetName()
                        + (changes.isNotEmpty() ? " (" + changes + ")" : juce::String());
        if (!writeWav(outDir.getChildFile(juce::File::createLegalFileName(name) + ".wav"), audio, sampleRate))
            return fail("could not write output for " + name);

        const auto s = measure(audio);
        std::printf("%-28s peak %6.1f dBFS   rms %6.1f dBFS\n", name.toRawUTF8(), s.peakDb, s.rmsDb);
    }

    std::printf("written to %s\n", outDir.getFullPathName().toRawUTF8());
    return 0;
}
