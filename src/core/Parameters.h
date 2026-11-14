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
    Leveler,
    DeEsser,
    Compressor,
    Multiband,
    Saturation,
    Exciter,
    Chorus,
    Delay,
    Reverb,
    Limiter,
    Tune, // added in v0.3: new modules go at the end so saved data stays valid
};

constexpr int kNumModules = 13;

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
inline constexpr const char* gateRatio = "gate_ratio";

// Auto Level (vocal leveler)
inline constexpr const char* levOn = "lev_on";
inline constexpr const char* levTarget = "lev_target";
inline constexpr const char* levRange = "lev_range";
inline constexpr const char* levSpeed = "lev_speed";

// Equalizer
inline constexpr const char* eqOn = "eq_on";
inline constexpr const char* eqLowCut = "eq_lowcut";
inline constexpr const char* eqLowCutSlope = "eq_lowcut_slope"; // 0 = 12 dB/oct, 1 = 24 dB/oct
inline constexpr const char* eqLowFreq = "eq_low_freq";
inline constexpr const char* eqLowGain = "eq_low_gain";
inline constexpr const char* eqMidFreq = "eq_mid_freq";
inline constexpr const char* eqMidGain = "eq_mid_gain";
inline constexpr const char* eqMidQ = "eq_mid_q";
// Two more bells for detailed work (0 dB = not in the signal path at all)
inline constexpr const char* eqLoMidFreq = "eq_lomid_freq";
inline constexpr const char* eqLoMidGain = "eq_lomid_gain";
inline constexpr const char* eqLoMidQ = "eq_lomid_q";
inline constexpr const char* eqHiMidFreq = "eq_himid_freq";
inline constexpr const char* eqHiMidGain = "eq_himid_gain";
inline constexpr const char* eqHiMidQ = "eq_himid_q";
inline constexpr const char* eqHighFreq = "eq_high_freq";
inline constexpr const char* eqHighGain = "eq_high_gain";
// Harshness tamer: a dynamic band that dips only while it gets too loud (Max Cut 0 = off)
// Plosive tamer: high-passes only during "p"/"b" thumps (0 = off)
inline constexpr const char* eqPlosive = "eq_plosive";
inline constexpr const char* eqTameFreq = "eq_tame_freq";
inline constexpr const char* eqTameThreshold = "eq_tame_threshold";
inline constexpr const char* eqTameRange = "eq_tame_range";

// Tune (pitch correction)
inline constexpr const char* tuneOn = "tune_on";
inline constexpr const char* tuneKey = "tune_key";             // C, C#, ... B
inline constexpr const char* tuneScale = "tune_scale";         // Chromatic, Major, ...
inline constexpr const char* tuneSpeed = "tune_speed";         // ms; 0 = instant ("hard tune")
inline constexpr const char* tuneAmount = "tune_amount";       // %
inline constexpr const char* tuneFormant = "tune_formant";     // semitones
inline constexpr const char* tuneTranspose = "tune_transpose"; // semitones
inline constexpr const char* tuneVoice = "tune_voice";         // High (female), Mid, Low (male)

// De-esser
inline constexpr const char* dsOn = "ds_on";
inline constexpr const char* dsFreq = "ds_freq";
inline constexpr const char* dsThreshold = "ds_threshold";
inline constexpr const char* dsRange = "ds_range";
inline constexpr const char* dsMode = "ds_mode"; // 0 = Band (narrow dynamic cut), 1 = Split (everything above Freq)
inline constexpr const char* dsAuto = "ds_auto"; // find the sibilant frequency automatically

// Compressor
inline constexpr const char* compOn = "comp_on";
inline constexpr const char* compThreshold = "comp_threshold";
inline constexpr const char* compRatio = "comp_ratio";
inline constexpr const char* compAttack = "comp_attack";
inline constexpr const char* compRelease = "comp_release";
inline constexpr const char* compMakeup = "comp_makeup";
inline constexpr const char* compMix = "comp_mix";

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
inline constexpr const char* satCharacter = "sat_character";
inline constexpr const char* satModel = "sat_model"; // 0 = Tube, 1 = Tape, 2 = Console

// Exciter
inline constexpr const char* exOn = "ex_on";
inline constexpr const char* exFreq = "ex_freq";
inline constexpr const char* exAmount = "ex_amount";
inline constexpr const char* exMix = "ex_mix";

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
inline constexpr const char* dlyDuck = "dly_duck";

// Reverb
inline constexpr const char* revOn = "rev_on";
inline constexpr const char* revSize = "rev_size";
inline constexpr const char* revDamping = "rev_damping";
inline constexpr const char* revWidth = "rev_width";
inline constexpr const char* revMix = "rev_mix";
inline constexpr const char* revPreDelay = "rev_predelay";
inline constexpr const char* revLowCut = "rev_lowcut";
inline constexpr const char* revHighCut = "rev_highcut";
inline constexpr const char* revDuck = "rev_duck";

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
    std::array<const char*, 24> params; // knobs, nullptr-terminated
};

const std::array<ModuleDescriptor, kNumModules>& moduleDescriptors();
const ModuleDescriptor& describe(ModuleId id);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Turns typed text into a value for a control of the given unit (see Parameters.cpp). */
float parseValue(const juce::String& text, int unitIndex);

} // namespace stagemic
