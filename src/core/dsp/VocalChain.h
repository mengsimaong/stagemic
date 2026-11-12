// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/Parameters.h"
#include "core/dsp/Module.h"

#include <array>
#include <memory>
#include <optional>

namespace stagemic::dsp
{

class Compressor;
class Gate;
class DeEsser;
class MultibandCompressor;
class Limiter;

/** Runs the built-in modules in a (re-orderable) order.

    - Switching a module on or off crossfades over 15 ms: no clicks.
    - A module that is off costs no CPU at all.
    - The order can be changed from any thread; the audio thread picks it up
      atomically at the start of the next block. */
class VocalChain
{
public:
    static constexpr int kSubBlock = 64;
    using Order = std::array<ModuleId, kNumModules>;

    explicit VocalChain(juce::AudioProcessorValueTreeState& state);
    ~VocalChain();

    void prepare(double sampleRate, int numChannels);
    void reset() noexcept;

    /** Audio thread. numSamples <= kSubBlock. */
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    /** Next block jumps straight to the current settings (used after loading saved state). */
    void requestSnap() noexcept { snapRequested.store(true, std::memory_order_release); }

    static Order defaultOrder() noexcept;
    static bool isValidOrder(const Order&) noexcept;
    bool setOrder(const Order&) noexcept;
    Order getOrder() const noexcept;

    static juce::String orderToString(const Order&);
    static std::optional<Order> orderFromString(const juce::String&);

    /** Live activity for the UI (0 when the module is off). Any thread. */
    struct Activity
    {
        float gateGain = 1.0f;          // 1 = open
        float deEsserReductionDb = 0.0f;
        float compressorReductionDb = 0.0f;
        float multibandReductionDb[3] = {0.0f, 0.0f, 0.0f};
        float limiterReductionDb = 0.0f;
    };
    Activity getActivity() const noexcept;
    float getCompressorGainReductionDb() const noexcept;
    bool isModuleActive(ModuleId id) const noexcept { return slots[(size_t)id].activeFlag.load(std::memory_order_relaxed); }

private:
    struct Slot
    {
        std::unique_ptr<Module> module;
        std::atomic<float>* enabled = nullptr;
        float fade = 0.0f;   // 0 = fully bypassed, 1 = fully on
        bool active = false; // module is running (on, or fading)
        std::atomic<bool> activeFlag{false}; // copy of 'active' for the UI
    };

    static std::uint64_t pack(const Order&) noexcept;
    static Order unpack(std::uint64_t) noexcept;

    std::array<Slot, kNumModules> slots;
    Compressor* compressor = nullptr;
    Gate* gate = nullptr;
    DeEsser* deEsser = nullptr;
    MultibandCompressor* multiband = nullptr;
    Limiter* limiter = nullptr;
    std::atomic<std::uint64_t> packedOrder;
    std::atomic<bool> snapRequested{true};
    float fadeStep = 1.0f;
    float dry[kMaxChannels][kSubBlock]{};
};

} // namespace stagemic::dsp
