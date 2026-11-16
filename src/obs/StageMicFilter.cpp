// SPDX-License-Identifier: AGPL-3.0-or-later

#include "obs/StageMicFilter.h"

#include "core/FilterInstance.h"
#include "obs/JuceRuntime.h"
#include "obs/ObsWindowOwner.h"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <atomic>
#include <cstdlib>
#include <memory>

#ifndef STAGEMIC_VERSION
    #define STAGEMIC_VERSION "dev"
#endif

namespace stagemic
{

namespace
{
constexpr const char* filterId = "stagemic_filter";
constexpr const char* stateKey = "stagemic_state";
constexpr const char* openButtonKey = "stagemic_open";
constexpr const char* infoKey = "stagemic_info";

struct FilterData
{
    obs_source_t* context = nullptr;

    // Owned here, released on the JUCE message thread
    std::shared_ptr<FilterInstance> instance;
    // Raw pointer for the audio thread
    FilterInstance* audioInstance = nullptr;

    int numChannels = 2;
    std::atomic<uint32_t> firstAudioFrames{0}; // set by the audio thread
    bool loggedFirstAudio = false;             // touched by video_tick only
};

void openEditor(struct FilterData* data);

const char* getName(void*)
{
    return obs_module_text("StageMic");
}

void* create(obs_data_t* settings, obs_source_t* source)
{
    if (!runtime::isRunning())
    {
        blog(LOG_ERROR, "[StageMic] Cannot create filter: JUCE runtime is not running");
        return nullptr;
    }

    auto* audio = obs_get_audio();
    const auto sampleRate = audio != nullptr ? audio_output_get_sample_rate(audio) : 48000u;
    const auto channels = audio != nullptr ? (int)audio_output_get_channels(audio) : 2;

    FilterInstance::Config config;
    config.sampleRate = (double)sampleRate;
    config.numHostChannels = channels;
    config.maxBlockSize = AUDIO_OUTPUT_FRAMES;
    config.hostDescription = juce::String("OBS Studio ") + obs_get_version_string();

    auto data = std::make_unique<FilterData>();
    data->context = source;
    data->numChannels = channels;

    try
    {
        data->instance = std::make_shared<FilterInstance>(config);
    }
    catch (const std::exception& e)
    {
        blog(LOG_ERROR, "[StageMic] Failed to create filter: %s", e.what());
        return nullptr;
    }

    data->audioInstance = data->instance.get();

    const char* saved = obs_data_get_string(settings, stateKey);
    if (saved != nullptr && *saved != '\0' && !data->instance->setState(saved))
        blog(LOG_WARNING, "[StageMic] Saved settings could not be read; using defaults");

    // Used by the CI smoke test
    if (const char* env = std::getenv("STAGEMIC_OPEN_EDITOR_ON_LOAD"); env != nullptr && *env == '1')
        openEditor(data.get());

    blog(LOG_INFO, "[StageMic] Filter created (%u Hz, %d OBS channels, processing %d)", sampleRate, channels,
         data->instance->getProcessedChannelCount());

    return data.release();
}

void destroy(void* ptr)
{
    std::unique_ptr<FilterData> data(static_cast<FilterData*>(ptr));
    if (data == nullptr)
        return;

    // OBS often calls destroy from a worker thread
    if (runtime::isRunning())
        runtime::post([instance = std::move(data->instance)]() mutable { instance.reset(); });

    blog(LOG_INFO, "[StageMic] Filter destroyed");
}

obs_audio_data* filterAudio(void* ptr, obs_audio_data* audio)
{
    // REAL-TIME AUDIO THREAD
    auto* data = static_cast<FilterData*>(ptr);
    if (data == nullptr || data->audioInstance == nullptr || audio == nullptr)
        return audio;

    data->audioInstance->process(reinterpret_cast<float* const*>(audio->data), data->numChannels, (int)audio->frames);

    // Tell the tick below that audio arrived
    data->firstAudioFrames.store(audio->frames, std::memory_order_relaxed);

    return audio;
}

// Runs once per video frame on OBS's graphics thread
void videoTick(void* ptr, float)
{
    auto* data = static_cast<FilterData*>(ptr);
    if (data == nullptr || data->loggedFirstAudio)
        return;

    if (const auto frames = data->firstAudioFrames.load(std::memory_order_relaxed); frames != 0)
    {
        data->loggedFirstAudio = true;
        blog(LOG_INFO, "[StageMic] Audio is flowing (block size %u frames)", frames);
    }
}

void save(void* ptr, obs_data_t* settings)
{
    auto* data = static_cast<FilterData*>(ptr);
    if (data == nullptr || data->instance == nullptr)
        return;

    obs_data_set_string(settings, stateKey, data->instance->getState().c_str());
}

juce::String makeWindowTitle(obs_source_t* filter)
{
    juce::String title = "Paopoa StageMic";
    if (auto* parent = obs_filter_get_parent(filter))
        if (const char* name = obs_source_get_name(parent))
            title << " - " << juce::String::fromUTF8(name);
    return title;
}

void openEditor(FilterData* data)
{
    if (data == nullptr || data->instance == nullptr)
        return;

    const auto title = makeWindowTitle(data->context);
    void* obsMainWindow = obs_frontend_get_main_window_handle();
    std::weak_ptr<FilterInstance> weak = data->instance;

    runtime::post([weak, title, obsMainWindow] {
        if (auto instance = weak.lock())
            instance->showEditor(title, [obsMainWindow](void* native) { setNativeWindowOwner(native, obsMainWindow); });
    });
}

bool openButtonClicked(obs_properties_t*, obs_property_t*, void* ptr)
{
    openEditor(static_cast<FilterData*>(ptr));
    return false; // no need for OBS to rebuild the properties view
}

obs_properties_t* getProperties(void* ptr)
{
    auto* props = obs_properties_create();
    obs_properties_add_button2(props, openButtonKey, obs_module_text("StageMic.Open"), openButtonClicked, ptr);

    juce::String info;
    if (auto* data = static_cast<FilterData*>(ptr); data != nullptr && data->instance != nullptr)
    {
        auto& proc = data->instance->getProcessor();
        info << "Paopoa StageMic " STAGEMIC_VERSION " - " << juce::String(proc.getPreparedSampleRate(), 0) << " Hz, "
             << data->instance->getProcessedChannelCount() << " ch, latency " << proc.getLatencySamples() << " samples";
    }
    else
    {
        info << "Paopoa StageMic " STAGEMIC_VERSION;
    }

    auto* infoProp = obs_properties_add_text(props, infoKey, info.toRawUTF8(), OBS_TEXT_INFO);
    obs_property_text_set_info_type(infoProp, OBS_TEXT_INFO_NORMAL);
    return props;
}

obs_source_info makeInfo()
{
    obs_source_info info{};
    info.id = filterId;
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = getName;
    info.create = create;
    info.destroy = destroy;
    info.filter_audio = filterAudio;
    info.video_tick = videoTick;
    info.save = save;
    info.get_properties = getProperties;
    return info;
}
} // namespace

void registerFilter()
{
    static const obs_source_info info = makeInfo();
    obs_register_source(&info);
}

} // namespace stagemic
