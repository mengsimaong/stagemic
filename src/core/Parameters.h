// SPDX-License-Identifier: AGPL-3.0-or-later
// Every user-facing StageMic setting: IDs, ranges, defaults and grouping.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace stagemic
{

/** The built-in modules, in their default processing order. */
enum class ModuleId : int
{
    Gate = 0,
    Equalizer,
    DeEsser,
    Compressor,
    Multiband,
    Saturation,
    Chorus,
    Delay,
    Reverb,
    Limiter,
};

constexpr int kNumModules = 10;

namespace pid
{
// Global
inline constexpr const char* inGain = "in_gain";
inline constexpr const char* outGain = "out_gain";

// Gate
inline constexpr const char* gateOn = "gate_on";
inline constexpr const char* gateThreshold = "gate_threshold";
inline constexpr const char* gateRange = "gate_range";
inline constexpr const char* gateRelease = "gate_release";

// Equalizer
inline constexpr const char* eqOn = "eq_on";
inline constexpr const char* eqLowCut = "eq_lowcut";
inline constexpr const char* eqLowFreq = "eq_low_freq";
inline constexpr const char* eqLowGain = "eq_low_gain";
inline constexpr const char* eqMidFreq = "eq_mid_freq";
inline constexpr const char* eqMidGain = "eq_mid_gain";
inline constexpr const char* eqMidQ = "eq_mid_q";
inline constexpr const char* eqHighFreq = "eq_high_freq";
inline constexpr const char* eqHighGain = "eq_high_gain";

// De-esser
inline constexpr const char* dsOn = "ds_on";
inline constexpr const char* dsFreq = "ds_freq";
inline constexpr const char* dsThreshold = "ds_threshold";
inline constexpr const char* dsRange = "ds_range";

// Compressor
inline constexpr const char* compOn = "comp_on";
inline constexpr const char* compThreshold = "comp_threshold";
inline constexpr const char* compRatio = "comp_ratio";
inline constexpr const char* compAttack = "comp_attack";
inline constexpr const char* compRelease = "comp_release";
inline constexpr const char* compMakeup = "comp_makeup";

// Multiband compressor
inline constexpr const char* mbOn = "mb_on";
inline constexpr const char* mbLowCross = "mb_low_cross";
inline constexpr const char* mbHighCross = "mb_high_cross";
inline constexpr const char* mbLowThreshold = "mb_low_threshold";
inline constexpr const char* mbMidThreshold = "mb_mid_threshold";
inline constexpr const char* mbHighThreshold = "mb_high_threshold";
inline constexpr const char* mbLowGain = "mb_low_gain";
inline constexpr const char* mbMidGain = "mb_mid_gain";
inline constexpr const char* mbHighGain = "mb_high_gain";
inline constexpr const char* mbRatio = "mb_ratio";
inline constexpr const char* mbAttack = "mb_attack";
inline constexpr const char* mbRelease = "mb_release";

// Saturation
inline constexpr const char* satOn = "sat_on";
inline constexpr const char* satDrive = "sat_drive";
inline constexpr const char* satMix = "sat_mix";

// Chorus
inline constexpr const char* chOn = "ch_on";
inline constexpr const char* chRate = "ch_rate";
inline constexpr const char* chDepth = "ch_depth";
inline constexpr const char* chMix = "ch_mix";

// Delay
inline constexpr const char* dlyOn = "dly_on";
inline constexpr const char* dlyTime = "dly_time";
inline constexpr const char* dlyFeedback = "dly_feedback";
inline constexpr const char* dlyTone = "dly_tone";
inline constexpr const char* dlyMix = "dly_mix";

// Reverb
inline constexpr const char* revOn = "rev_on";
inline constexpr const char* revSize = "rev_size";
inline constexpr const char* revDamping = "rev_damping";
inline constexpr const char* revWidth = "rev_width";
inline constexpr const char* revMix = "rev_mix";

// Limiter
inline constexpr const char* limOn = "lim_on";
inline constexpr const char* limCeiling = "lim_ceiling";
inline constexpr const char* limRelease = "lim_release";
} // namespace pid

/** Describes one module for the UI and for presets. */
struct ModuleDescriptor
{
    ModuleId id;
    const char* key;          // stable identifier used in saved module order
    const char* displayName;  // shown in the UI
    const char* enableParam;  // on/off switch
    std::array<const char*, 12> params; // knobs, nullptr-terminated
};

const std::array<ModuleDescriptor, kNumModules>& moduleDescriptors();
const ModuleDescriptor& describe(ModuleId id);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Turns typed text into a value for a control of the given unit (see Parameters.cpp). */
float parseValue(const juce::String& text, int unitIndex);

} // namespace stagemic
