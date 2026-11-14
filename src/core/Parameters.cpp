// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/Parameters.h"

namespace stagemic
{

namespace
{
enum class Unit
{
    dB,
    Hz,
    ms,
    percent,
    ratio,
    plain,
    semitones,
};

struct FloatSpec
{
    const char* id;
    const char* name;
    float min, max, def;
    float centre; // 0 = linear knob; otherwise the value at the knob's midpoint
    Unit unit;
};

struct BoolSpec
{
    const char* id;
    const char* name;
    bool def;
};

// clang-format off
const FloatSpec floatSpecs[] = {
    {pid::inGain,          "Input Gain",        -24.0f, 24.0f,    0.0f,    0.0f,   Unit::dB},
    {pid::outGain,         "Output Gain",       -24.0f, 24.0f,    0.0f,    0.0f,   Unit::dB},

    {pid::gateThreshold,   "Gate Threshold",    -80.0f, -10.0f,  -55.0f,   0.0f,   Unit::dB},
    {pid::gateRange,       "Gate Range",          0.0f,  60.0f,   24.0f,   0.0f,   Unit::dB},
    {pid::gateRelease,     "Gate Release",       20.0f, 1000.0f, 200.0f, 150.0f,   Unit::ms},
    {pid::gateRatio,       "Gate Ratio",          1.5f,   10.0f,   2.0f,   3.0f,   Unit::ratio},

    {pid::levTarget,       "Level Target",      -30.0f,  -10.0f, -18.0f,   0.0f,   Unit::dB},
    {pid::levRange,        "Level Range",         0.0f,   18.0f,   9.0f,   0.0f,   Unit::dB},
    {pid::levSpeed,        "Level Speed",       200.0f, 4000.0f, 1000.0f, 1000.0f, Unit::ms},

    {pid::eqLowCut,        "Low Cut",            20.0f,  400.0f,  80.0f, 100.0f,   Unit::Hz},
    {pid::eqLowFreq,       "Low Shelf Freq",     40.0f,  500.0f, 150.0f, 150.0f,   Unit::Hz},
    {pid::eqLowGain,       "Low Shelf Gain",    -12.0f,   12.0f,   0.0f,   0.0f,   Unit::dB},
    {pid::eqMidFreq,       "Mid Freq",          200.0f, 8000.0f, 2500.0f, 1500.0f, Unit::Hz},
    {pid::eqMidGain,       "Mid Gain",          -12.0f,   12.0f,   0.0f,   0.0f,   Unit::dB},
    {pid::eqMidQ,          "Mid Width (Q)",       0.3f,    4.0f,   1.0f,   1.0f,   Unit::plain},
    {pid::eqLoMidFreq,     "Low Mid Freq",       80.0f, 2000.0f,  400.0f,  400.0f, Unit::Hz},
    {pid::eqLoMidGain,     "Low Mid Gain",      -12.0f,   12.0f,   0.0f,   0.0f,   Unit::dB},
    {pid::eqLoMidQ,        "Low Mid Width (Q)",   0.3f,    8.0f,   1.0f,   1.5f,   Unit::plain},
    {pid::eqHiMidFreq,     "High Mid Freq",    1000.0f, 16000.0f, 5000.0f, 5000.0f, Unit::Hz},
    {pid::eqHiMidGain,     "High Mid Gain",     -12.0f,   12.0f,   0.0f,   0.0f,   Unit::dB},
    {pid::eqHiMidQ,        "High Mid Width (Q)",  0.3f,    8.0f,   1.0f,   1.5f,   Unit::plain},
    {pid::eqHighFreq,      "Air Freq",         2000.0f, 16000.0f, 9000.0f, 7000.0f, Unit::Hz},
    {pid::eqHighGain,      "Air Gain",          -12.0f,   12.0f,   2.0f,   0.0f,   Unit::dB},
    {pid::eqPlosive,       "Plosive Tamer",       0.0f,  100.0f,  50.0f,   0.0f,   Unit::percent},
    {pid::eqTameFreq,      "Tame Freq",        1500.0f, 6000.0f, 3200.0f, 3000.0f, Unit::Hz},
    {pid::eqTameThreshold, "Tame Threshold",    -24.0f,    6.0f,  -3.0f,   0.0f,   Unit::dB},
    {pid::eqTameRange,     "Tame Max Cut",        0.0f,   12.0f,   0.0f,   0.0f,   Unit::dB},

    {pid::dsFreq,          "De-Ess Freq",      3000.0f, 12000.0f, 6500.0f, 6500.0f, Unit::Hz},
    {pid::dsThreshold,     "De-Ess Threshold",  -50.0f,    0.0f, -28.0f,   0.0f,   Unit::dB},
    {pid::dsRange,         "De-Ess Max Cut",      0.0f,   18.0f,   8.0f,   0.0f,   Unit::dB},

    {pid::compThreshold,   "Comp Threshold",    -50.0f,    0.0f, -20.0f,   0.0f,   Unit::dB},
    {pid::compRatio,       "Comp Ratio",          1.0f,   12.0f,   3.0f,   3.0f,   Unit::ratio},
    {pid::compAttack,      "Comp Attack",         0.5f,   80.0f,   6.0f,   8.0f,   Unit::ms},
    {pid::compRelease,     "Comp Release",       20.0f,  800.0f, 120.0f, 150.0f,   Unit::ms},
    {pid::compMakeup,      "Comp Makeup",         0.0f,   18.0f,   4.0f,   0.0f,   Unit::dB},
    {pid::compMix,         "Comp Mix",            0.0f,  100.0f, 100.0f,   0.0f,   Unit::percent},

    {pid::mbLowCross,      "Low/Mid Split",      60.0f,  800.0f, 220.0f, 220.0f,   Unit::Hz},
    {pid::mbHighCross,     "Mid/High Split",   1500.0f, 10000.0f, 4000.0f, 4000.0f, Unit::Hz},
    {pid::mbLowThreshold,  "Low Threshold",     -50.0f,    0.0f, -24.0f,   0.0f,   Unit::dB},
    {pid::mbMidThreshold,  "Mid Threshold",     -50.0f,    0.0f, -24.0f,   0.0f,   Unit::dB},
    {pid::mbHighThreshold, "High Threshold",    -50.0f,    0.0f, -24.0f,   0.0f,   Unit::dB},
    {pid::mbLowGain,       "Low Gain",          -12.0f,   12.0f,   0.0f,   0.0f,   Unit::dB},
    {pid::mbMidGain,       "Mid Gain",          -12.0f,   12.0f,   0.0f,   0.0f,   Unit::dB},
    {pid::mbHighGain,      "High Gain",         -12.0f,   12.0f,   0.0f,   0.0f,   Unit::dB},
    {pid::mbRatio,         "Multiband Ratio",     1.0f,    8.0f,   2.5f,   2.5f,   Unit::ratio},
    {pid::mbAttack,        "Multiband Attack",    1.0f,   80.0f,  10.0f,  10.0f,   Unit::ms},
    {pid::mbRelease,       "Multiband Release",  20.0f,  800.0f, 150.0f, 150.0f,   Unit::ms},

    {pid::satDrive,        "Drive",               0.0f,   24.0f,   8.0f,   0.0f,   Unit::dB},
    {pid::satMix,          "Saturation Mix",      0.0f,  100.0f,  40.0f,   0.0f,   Unit::percent},
    {pid::satCharacter,    "Saturation Character", 0.0f, 100.0f,  35.0f,   0.0f,   Unit::percent},

    {pid::exFreq,          "Exciter Freq",     2000.0f, 12000.0f, 5000.0f, 5000.0f, Unit::Hz},
    {pid::exAmount,        "Exciter Amount",      0.0f,  100.0f,  50.0f,   0.0f,   Unit::percent},
    {pid::exMix,           "Exciter Mix",         0.0f,  100.0f,  25.0f,   0.0f,   Unit::percent},

    {pid::chRate,          "Chorus Rate",         0.05f,   4.0f,   0.6f,   0.8f,   Unit::Hz},
    {pid::chDepth,         "Chorus Depth",        0.0f,  100.0f,  35.0f,   0.0f,   Unit::percent},
    {pid::chMix,           "Chorus Mix",          0.0f,  100.0f,  30.0f,   0.0f,   Unit::percent},

    {pid::dlyTime,         "Delay Time",         30.0f, 1200.0f, 330.0f, 300.0f,   Unit::ms},
    {pid::dlyFeedback,     "Delay Feedback",      0.0f,   90.0f,  28.0f,   0.0f,   Unit::percent},
    {pid::dlyTone,         "Delay Tone",       1000.0f, 16000.0f, 5000.0f, 5000.0f, Unit::Hz},
    {pid::dlyMix,          "Delay Mix",           0.0f,  100.0f,  18.0f,   0.0f,   Unit::percent},
    {pid::dlyDuck,         "Delay Duck",          0.0f,   24.0f,   8.0f,   0.0f,   Unit::dB},

    {pid::revSize,         "Reverb Size",         0.0f,  100.0f,  55.0f,   0.0f,   Unit::percent},
    {pid::revDamping,      "Reverb Damping",      0.0f,  100.0f,  45.0f,   0.0f,   Unit::percent},
    {pid::revWidth,        "Reverb Width",        0.0f,  100.0f, 100.0f,   0.0f,   Unit::percent},
    {pid::revMix,          "Reverb Mix",          0.0f,  100.0f,  18.0f,   0.0f,   Unit::percent},
    {pid::revPreDelay,     "Reverb Pre-Delay",    0.0f,  150.0f,  30.0f,  40.0f,   Unit::ms},
    {pid::revLowCut,       "Reverb Low Cut",     50.0f, 1000.0f, 250.0f, 250.0f,   Unit::Hz},
    {pid::revHighCut,      "Reverb High Cut",  2000.0f, 16000.0f, 9000.0f, 7000.0f, Unit::Hz},
    {pid::revDuck,         "Reverb Duck",         0.0f,   24.0f,   4.0f,   0.0f,   Unit::dB},

    {pid::tuneSpeed,       "Tune Speed",          0.0f,  400.0f,  20.0f,  60.0f,   Unit::ms},
    {pid::tuneAmount,      "Tune Amount",         0.0f,  100.0f, 100.0f,   0.0f,   Unit::percent},
    {pid::tuneFormant,     "Tune Formant",       -5.0f,    5.0f,   0.0f,   0.0f,   Unit::semitones},

    {pid::limCeiling,      "Limiter Ceiling",   -12.0f,    0.0f,  -1.0f,   0.0f,   Unit::dB},
    {pid::limRelease,      "Limiter Release",    10.0f,  500.0f,  60.0f,  60.0f,   Unit::ms},
};

const BoolSpec boolSpecs[] = {
    {pid::gateOn,  "Gate On",        true},
    {pid::levOn,   "Auto Level On",  true},
    {pid::eqOn,    "EQ On",          true},
    {pid::dsOn,    "De-Esser On",    true},
    {pid::dsAuto,  "De-Ess Auto Freq", false},
    {pid::compOn,  "Compressor On",  true},
    {pid::mbOn,    "Multiband On",   false},
    {pid::satOn,   "Saturation On",  false},
    {pid::exOn,    "Exciter On",     false},
    {pid::chOn,    "Chorus On",      false},
    {pid::dlyOn,   "Delay On",       false},
    {pid::revOn,   "Reverb On",      true},
    {pid::limOn,   "Limiter On",     true},
    {pid::tuneOn,  "Tune On",        false},
};

const std::array<ModuleDescriptor, kNumModules> descriptors{{
    {ModuleId::Gate,       "gate",       "Gate",        pid::gateOn, {pid::gateThreshold, pid::gateRange, pid::gateRelease, pid::gateRatio}},
    {ModuleId::Equalizer,  "eq",         "EQ",          pid::eqOn,   {pid::eqLowCut, pid::eqLowFreq, pid::eqLowGain, pid::eqMidFreq, pid::eqMidGain, pid::eqMidQ, pid::eqLoMidFreq, pid::eqLoMidGain, pid::eqLoMidQ, pid::eqHiMidFreq, pid::eqHiMidGain, pid::eqHiMidQ, pid::eqHighFreq, pid::eqHighGain, pid::eqPlosive, pid::eqTameFreq, pid::eqTameThreshold, pid::eqTameRange}},
    {ModuleId::Leveler,    "leveler",    "Auto Level",  pid::levOn,  {pid::levTarget, pid::levRange, pid::levSpeed}},
    {ModuleId::DeEsser,    "deesser",    "De-Esser",    pid::dsOn,   {pid::dsFreq, pid::dsThreshold, pid::dsRange}},
    {ModuleId::Compressor, "compressor", "Compressor",  pid::compOn, {pid::compThreshold, pid::compRatio, pid::compAttack, pid::compRelease, pid::compMakeup, pid::compMix}},
    {ModuleId::Multiband,  "multiband",  "Multiband",   pid::mbOn,   {pid::mbLowCross, pid::mbHighCross, pid::mbLowThreshold, pid::mbMidThreshold, pid::mbHighThreshold, pid::mbLowGain, pid::mbMidGain, pid::mbHighGain, pid::mbRatio, pid::mbAttack, pid::mbRelease}},
    {ModuleId::Saturation, "saturation", "Saturation",  pid::satOn,  {pid::satDrive, pid::satMix, pid::satCharacter}},
    {ModuleId::Exciter,    "exciter",    "Exciter",     pid::exOn,   {pid::exFreq, pid::exAmount, pid::exMix}},
    {ModuleId::Chorus,     "chorus",     "Chorus",      pid::chOn,   {pid::chRate, pid::chDepth, pid::chMix}},
    {ModuleId::Delay,      "delay",      "Delay",       pid::dlyOn,  {pid::dlyTime, pid::dlyFeedback, pid::dlyTone, pid::dlyMix, pid::dlyDuck}},
    {ModuleId::Reverb,     "reverb",     "Reverb",      pid::revOn,  {pid::revSize, pid::revDamping, pid::revWidth, pid::revMix, pid::revPreDelay, pid::revLowCut, pid::revHighCut, pid::revDuck}},
    {ModuleId::Limiter,    "limiter",    "Limiter",     pid::limOn,  {pid::limCeiling, pid::limRelease}},
    {ModuleId::Tune,       "tune",       "Tune",        pid::tuneOn, {pid::tuneKey, pid::tuneScale, pid::tuneSpeed, pid::tuneAmount, pid::tuneFormant, pid::tuneTranspose, pid::tuneVoice}},
}};
// clang-format on

juce::String formatValue(float v, Unit unit)
{
    switch (unit)
    {
        case Unit::dB:
            return (v > 0.0f ? "+" : "") + juce::String(v, 1) + " dB";
        case Unit::Hz:
            return v >= 1000.0f ? juce::String(v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                : juce::String(v, v < 10.0f ? 2 : 0) + " Hz";
        case Unit::ms:
            return juce::String(v, v < 10.0f ? 1 : 0) + " ms";
        case Unit::percent:
            return juce::String(juce::roundToInt(v)) + " %";
        case Unit::ratio:
            return juce::String(v, 1) + " : 1";
        case Unit::semitones:
            return (v > 0.05f ? "+" : "") + juce::String(v, std::abs(v - std::round(v)) < 0.05f ? 0 : 1) + " st";
        case Unit::plain:
            break;
    }
    return juce::String(v, 2);
}
} // namespace

float parseValue(const juce::String& typed, int unitIndex)
{
    // Accepts what people naturally type: "2.5k", "3 kHz", "800hz", "-6", "+4.5 dB", "4:1", "120 ms", "1.2 s", "35%".
    const auto text = typed.trim().toLowerCase().removeCharacters(" ");
    const auto beforeColon = text.upToFirstOccurrenceOf(":", false, false); // "4:1" -> "4"
    const auto number = beforeColon.retainCharacters("0123456789.,-+").replaceCharacter(',', '.');
    float v = number.getFloatValue();

    const auto unit = (Unit)unitIndex;
    if (unit == Unit::Hz && text.containsChar('k'))
        v *= 1000.0f;
    else if (unit == Unit::ms && text.endsWith("s") && !text.endsWith("ms"))
        v *= 1000.0f; // seconds typed into a milliseconds control
    return v;
}

const std::array<ModuleDescriptor, kNumModules>& moduleDescriptors()
{
    return descriptors;
}

const ModuleDescriptor& describe(ModuleId id)
{
    return descriptors[(size_t)id];
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& s : floatSpecs)
    {
        juce::NormalisableRange<float> range(s.min, s.max);
        if (s.centre > s.min && s.centre < s.max)
            range.setSkewForCentre(s.centre);

        const auto unit = s.unit;
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withStringFromValueFunction([unit](float v, int) { return formatValue(v, unit); })
                              .withValueFromStringFunction([unit](const juce::String& text) { return parseValue(text, (int)unit); });

        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{s.id, 1}, s.name, range, s.def,
                                                               attributes));
    }

    for (const auto& s : boolSpecs)
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{s.id, 1}, s.name, s.def));

    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{pid::eqLowCutSlope, 1}, "Low Cut Slope",
                                                            juce::StringArray{"12 dB/oct", "24 dB/oct"}, 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{pid::dsMode, 1}, "De-Ess Mode",
                                                            juce::StringArray{"Band", "Split"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{pid::satModel, 1}, "Saturation Model",
                                                            juce::StringArray{"Tube", "Tape", "Console"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{pid::tuneKey, 1}, "Tune Key",
        juce::StringArray{"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{pid::tuneScale, 1}, "Tune Scale",
        juce::StringArray{"Chromatic", "Major", "Minor", "Harmonic Minor", "Major Pentatonic", "Minor Pentatonic", "Blues", "Robot (one note)"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{pid::tuneVoice, 1}, "Tune Voice",
                                                            juce::StringArray{"High", "Mid", "Low"}, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{pid::tuneTranspose, 1}, "Tune Pitch", -12, 12, 0,
        juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) {
            return (v > 0 ? "+" : "") + juce::String(v) + " st";
        }).withValueFromStringFunction([](const juce::String& t) { return t.retainCharacters("0123456789-+").getIntValue(); })));

    return layout;
}

} // namespace stagemic
