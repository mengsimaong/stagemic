// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/dsp/VocalChain.h"

#include "core/dsp/Dynamics.h"
#include "core/dsp/Effects.h"
#include "core/dsp/Tone.h"
#include "core/dsp/Tune.h"

#include <vector>

namespace stagemic::dsp
{

VocalChain::VocalChain(juce::AudioProcessorValueTreeState& state)
    : packedOrder(pack(defaultOrder()))
    , activeOrder(pack(defaultOrder()))
{
    auto make = [&](ModuleId id) -> std::unique_ptr<Module> {
        switch (id)
        {
            case ModuleId::Gate:
                return std::make_unique<Gate>(state);
            case ModuleId::Equalizer:
                return std::make_unique<Equalizer>(state);
            case ModuleId::Leveler:
                return std::make_unique<Leveler>(state);
            case ModuleId::DeEsser:
                return std::make_unique<DeEsser>(state, deEsserListen);
            case ModuleId::Compressor:
                return std::make_unique<Compressor>(state);
            case ModuleId::Multiband:
                return std::make_unique<MultibandCompressor>(state);
            case ModuleId::Saturation:
                return std::make_unique<Saturation>(state);
            case ModuleId::Exciter:
                return std::make_unique<Exciter>(state);
            case ModuleId::Chorus:
                return std::make_unique<Chorus>(state);
            case ModuleId::Delay:
                return std::make_unique<Delay>(state);
            case ModuleId::Reverb:
                return std::make_unique<Reverb>(state);
            case ModuleId::Limiter:
                return std::make_unique<Limiter>(state);
            case ModuleId::Tune:
                return std::make_unique<Tune>(state);
        }
        return {};
    };

    for (const auto& d : moduleDescriptors())
    {
        auto& slot = slots[(size_t)d.id];
        slot.module = make(d.id);
        slot.enabled = state.getRawParameterValue(d.enableParam);
        jassert(slot.module != nullptr && slot.enabled != nullptr);
    }

    compressor = static_cast<Compressor*>(slots[(size_t)ModuleId::Compressor].module.get());
    gate = static_cast<Gate*>(slots[(size_t)ModuleId::Gate].module.get());
    leveler = static_cast<Leveler*>(slots[(size_t)ModuleId::Leveler].module.get());
    deEsser = static_cast<DeEsser*>(slots[(size_t)ModuleId::DeEsser].module.get());
    equalizer = static_cast<Equalizer*>(slots[(size_t)ModuleId::Equalizer].module.get());
    multiband = static_cast<MultibandCompressor*>(slots[(size_t)ModuleId::Multiband].module.get());
    limiter = static_cast<Limiter*>(slots[(size_t)ModuleId::Limiter].module.get());
    tune = static_cast<Tune*>(slots[(size_t)ModuleId::Tune].module.get());
}

VocalChain::~VocalChain() = default;

void VocalChain::prepare(double sampleRate, int numChannels)
{
    for (auto& slot : slots)
        slot.module->prepare(sampleRate, juce::jlimit(1, kMaxChannels, numChannels));

    fadeStep = (float)(1.0 / (0.015 * sampleRate));
    reset();
    requestSnap();
}

void VocalChain::reset() noexcept
{
    for (auto& slot : slots)
    {
        slot.module->reset();
        slot.active = false;
        slot.activeFlag.store(false, std::memory_order_relaxed);
        slot.fade = 0.0f;
    }
}

float VocalChain::getCompressorGainReductionDb() const noexcept
{
    return isModuleActive(ModuleId::Compressor) ? compressor->getGainReductionDb() : 0.0f;
}

VocalChain::Activity VocalChain::getActivity() const noexcept
{
    Activity a;
    if (isModuleActive(ModuleId::Gate))
        a.gateGain = gate->getGain();
    if (isModuleActive(ModuleId::Leveler))
        a.levelerGainDb = leveler->getGainDb();
    if (isModuleActive(ModuleId::Equalizer))
    {
        a.tameReductionDb = equalizer->getTameReductionDb();
        a.plosiveAmount = equalizer->getPlosiveActivity();
    }
    if (isModuleActive(ModuleId::DeEsser))
    {
        a.deEsserReductionDb = deEsser->getGainReductionDb();
        a.deEsserFrequency = deEsser->getFrequency();
    }
    a.compressorReductionDb = getCompressorGainReductionDb();
    if (isModuleActive(ModuleId::Multiband))
        for (int b = 0; b < 3; ++b)
            a.multibandReductionDb[b] = multiband->getGainReductionDb(b);
    if (isModuleActive(ModuleId::Limiter))
        a.limiterReductionDb = limiter->getGainReductionDb();
    return a;
}

void VocalChain::process(float* const* ch, int numChannels, int n) noexcept
{
    jassert(n <= kSubBlock);
    numChannels = juce::jlimit(0, kMaxChannels, numChannels);
    if (numChannels == 0 || n <= 0)
        return;

    const bool snap = snapRequested.exchange(false, std::memory_order_acq_rel);

    // A new order is applied click-free
    const auto requested = packedOrder.load(std::memory_order_acquire);
    float outputRampFrom = 1.0f, outputRampTo = 1.0f;
    if (snap)
    {
        activeOrder = requested;
        reorderFadingOut = false;
    }
    else if (reorderFadingOut)
    {
        activeOrder = requested; // second half
        reorderFadingOut = false;
        outputRampFrom = 0.0f;
    }
    else if (requested != activeOrder)
    {
        reorderFadingOut = true; // first half
        outputRampTo = 0.0f;
    }
    const auto order = unpack(activeOrder);
    bool returnsPending = false;

    // Adds the summed send effects back onto the voice
    auto flushReturns = [&] {
        if (!returnsPending)
            return;
        for (int c = 0; c < numChannels; ++c)
            for (int i = 0; i < n; ++i)
                ch[c][i] += returns[c][i];
        returnsPending = false;
    };

    for (const auto id : order)
    {
        auto& slot = slots[(size_t)id];
        const bool wanted = load(slot.enabled) >= 0.5f;

        if (snap)
        {
            if (wanted)
            {
                slot.module->reset();
                slot.module->update(true);
            }
            slot.active = wanted;
            slot.fade = wanted ? 1.0f : 0.0f;
        }
        else if (wanted && !slot.active)
        {
            // Switching on
            slot.module->reset();
            slot.module->update(true);
            slot.active = true;
            slot.fade = 0.0f;
        }

        const bool isSend = slot.module->isSend();
        if (!isSend)
            flushReturns(); // the next insert hears the voice plus all

        if (!slot.active)
            continue;

        if (!snap)
            slot.module->update(false);

        const float target = wanted ? 1.0f : 0.0f;
        const float step = wanted ? fadeStep : -fadeStep;

        if (isSend)
        {
            // Parallel effect
            float* send[kMaxChannels] = {sendBuffer[0], sendBuffer[1]};
            for (int c = 0; c < numChannels; ++c)
                std::copy(ch[c], ch[c] + n, sendBuffer[c]);
            slot.module->process(send, numChannels, n);

            if (!returnsPending)
            {
                for (int c = 0; c < numChannels; ++c)
                    std::fill(returns[c], returns[c] + n, 0.0f);
                returnsPending = true;
            }

            float fade = slot.fade;
            for (int i = 0; i < n; ++i)
            {
                if (fade != target)
                    fade = juce::jlimit(0.0f, 1.0f, fade + step);
                for (int c = 0; c < numChannels; ++c)
                    returns[c][i] += fade * sendBuffer[c][i];
            }
            slot.fade = fade;
            if (fade <= 0.0f)
                slot.active = false;
            continue;
        }

        if (slot.fade == target)
        {
            // Fully on
            slot.module->process(ch, numChannels, n);
            continue;
        }

        // Crossfading between the dry input and the module's output
        for (int c = 0; c < numChannels; ++c)
            std::copy(ch[c], ch[c] + n, dry[c]);

        slot.module->process(ch, numChannels, n);

        float fade = slot.fade;
        for (int i = 0; i < n; ++i)
        {
            fade = juce::jlimit(0.0f, 1.0f, fade + step);
            for (int c = 0; c < numChannels; ++c)
                ch[c][i] = dry[c][i] + fade * (ch[c][i] - dry[c][i]);
        }

        slot.fade = fade;
        if (fade <= 0.0f)
            slot.active = false; // fully off
    }

    flushReturns();

    if (outputRampFrom != 1.0f || outputRampTo != 1.0f)
        for (int i = 0; i < n; ++i)
        {
            const float g = outputRampFrom + (outputRampTo - outputRampFrom) * (float)(i + 1) / (float)n;
            for (int c = 0; c < numChannels; ++c)
                ch[c][i] *= g;
        }

    for (auto& slot : slots)
        slot.activeFlag.store(slot.active, std::memory_order_relaxed);
}

//==============================================================================
VocalChain::Order VocalChain::defaultOrder() noexcept
{
    // The recommended vocal chain
    return {ModuleId::Gate,      ModuleId::Tune,       ModuleId::Equalizer, ModuleId::Leveler, ModuleId::Compressor,
            ModuleId::Multiband, ModuleId::Saturation, ModuleId::Exciter,   ModuleId::DeEsser, ModuleId::Chorus,
            ModuleId::Delay,     ModuleId::Reverb,     ModuleId::Limiter};
}

juce::String VocalChain::legacyDefaultOrderString()
{
    return "gate,eq,deesser,compressor,multiband,saturation,chorus,delay,reverb,limiter"; // v0.2.0's default
}

bool VocalChain::isValidOrder(const Order& order) noexcept
{
    std::array<bool, kNumModules> seen{};
    for (const auto id : order)
    {
        const int i = (int)id;
        if (i < 0 || i >= kNumModules || seen[(size_t)i])
            return false;
        seen[(size_t)i] = true;
    }
    return true;
}

bool VocalChain::setOrder(const Order& order) noexcept
{
    if (!isValidOrder(order))
        return false;
    packedOrder.store(pack(order), std::memory_order_release);
    return true;
}

VocalChain::Order VocalChain::getOrder() const noexcept
{
    return unpack(packedOrder.load(std::memory_order_acquire));
}

std::uint64_t VocalChain::pack(const Order& order) noexcept
{
    std::uint64_t packed = 0;
    for (int i = 0; i < kNumModules; ++i)
        packed |= (std::uint64_t)((int)order[(size_t)i] & 0xF) << (4 * i);
    return packed;
}

VocalChain::Order VocalChain::unpack(std::uint64_t packed) noexcept
{
    Order order{};
    for (int i = 0; i < kNumModules; ++i)
        order[(size_t)i] = (ModuleId)((packed >> (4 * i)) & 0xF);
    return order;
}

juce::String VocalChain::orderToString(const Order& order)
{
    juce::StringArray keys;
    for (const auto id : order)
        keys.add(describe(id).key);
    return keys.joinIntoString(",");
}

std::optional<VocalChain::Order> VocalChain::orderFromString(const juce::String& text)
{
    juce::StringArray keys;
    keys.addTokens(text, ",", "");
    keys.trim();
    keys.removeEmptyStrings();

    // Older presets may not know about newer modules
    std::vector<ModuleId> list;
    std::array<bool, kNumModules> used{};

    for (const auto& key : keys)
    {
        for (const auto& d : moduleDescriptors())
        {
            if (key == d.key)
            {
                if (used[(size_t)d.id])
                    return std::nullopt; // duplicate
                used[(size_t)d.id] = true;
                list.push_back(d.id);
                break;
            }
        }
    }

    if (list.empty())
        return std::nullopt;

    const auto defaults = defaultOrder();
    for (size_t i = 0; i < defaults.size(); ++i)
    {
        const auto id = defaults[i];
        if (used[(size_t)id])
            continue;

        auto insertAt = list.begin(); // no listed predecessor
        for (size_t j = i; j-- > 0;)
        {
            const auto found = std::find(list.begin(), list.end(), defaults[j]);
            if (found != list.end())
            {
                insertAt = found + 1;
                break;
            }
        }
        list.insert(insertAt, id);
        used[(size_t)id] = true;
    }

    Order order{};
    std::copy(list.begin(), list.end(), order.begin());
    return order;
}

} // namespace stagemic::dsp
