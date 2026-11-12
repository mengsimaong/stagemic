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
class Leveler;
class DeEsser;
class Equalizer;
class Tune;
class MultibandCompressor;
class Limiter;

/** Runs the built-in modules in a (re-orderable) order.

    Insert modules (gate, EQ, dynamics, saturation, limiter) process the voice in
    series. Send modules (chorus, delay, reverb) that sit next to each other in
    the order all receive the same dry voice in parallel; their effect signals are
    summed and mixed back in before the next insert module, the way a mixing
    engineer uses effect sends.

    - Switching a module on or off crossfades over 15 ms: no clicks.
    - A module that is off costs no CPU at all.
    - The order can be changed from any thread; the audio thread switches to it
      with a ~1.3 ms fade out and back in, so re-ordering while singing doesn't click. */
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

    /** The recommended order (what "Recommended order" in the window restores). */
    static Order defaultOrder() noexcept;
    /** The default order of v0.2.0, used to move old setups onto the new default. */
    static juce::String legacyDefaultOrderString();
    static bool isValidOrder(const Order&) noexcept;
    bool setOrder(const Order&) noexcept;
    Order getOrder() const noexcept;

    static juce::String orderToString(const Order&);
    static std::optional<Order> orderFromString(const juce::String&);

    /** Live activity for the UI (0 when the module is off). Any thread. */
    struct Activity
    {
        float gateGain = 1.0f;          // 1 = open
        float levelerGainDb = 0.0f;     // + = boosting, - = cutting
        float deEsserReductionDb = 0.0f;
        float tameReductionDb = 0.0f;   // EQ harshness tamer
        float plosiveAmount = 0.0f;     // EQ plosive tamer, 0..1
        float deEsserFrequency = 6500.0f;
        float compressorReductionDb = 0.0f;
        float multibandReductionDb[3] = {0.0f, 0.0f, 0.0f};
        float limiterReductionDb = 0.0f;
    };
    Activity getActivity() const noexcept;
    float getCompressorGainReductionDb() const noexcept;
    /** De-esser "Listen": hear only what the de-esser hears. A temporary tuning aid,
        not a saved setting. Any thread. */
    void setDeEsserListen(bool on) noexcept { deEsserListen.store(on, std::memory_order_relaxed); }
    bool getDeEsserListen() const noexcept { return deEsserListen.load(std::memory_order_relaxed); }

    /** Pitch correction (for its graph and the latency read-out). */
    const Tune& getTune() const noexcept { return *tune; }

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
    Leveler* leveler = nullptr;
    DeEsser* deEsser = nullptr;
    Equalizer* equalizer = nullptr;
    Tune* tune = nullptr;
    MultibandCompressor* multiband = nullptr;
    Limiter* limiter = nullptr;
    std::atomic<std::uint64_t> packedOrder; // requested order (any thread)
    std::uint64_t activeOrder;              // order in use (audio thread)
    bool reorderFadingOut = false;
    std::atomic<bool> snapRequested{true};
    std::atomic<bool> deEsserListen{false};
    float fadeStep = 1.0f;
    float dry[kMaxChannels][kSubBlock]{};
    float sendBuffer[kMaxChannels][kSubBlock]{};
    float returns[kMaxChannels][kSubBlock]{};
};

} // namespace stagemic::dsp
