// SPDX-License-Identifier: AGPL-3.0-or-later
#include "obs/JuceRuntime.h"
#include "obs/StageMicFilter.h"

#include <obs-module.h>

#ifndef STAGEMIC_VERSION
  #define STAGEMIC_VERSION "dev"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("stagemic", "en-US")
OBS_MODULE_AUTHOR("mengsimaong")

MODULE_EXPORT const char* obs_module_name(void)
{
    return "Paopoa StageMic";
}

MODULE_EXPORT const char* obs_module_description(void)
{
    return "One-window karaoke vocal chain for streamers and VTubers.";
}

bool obs_module_load(void)
{
    blog(LOG_INFO, "[StageMic] Loading version %s", STAGEMIC_VERSION);

    if (!stagemic::runtime::start())
    {
        blog(LOG_ERROR, "[StageMic] Failed to start the JUCE runtime; StageMic will not be available");
        return false;
    }

    stagemic::registerFilter();
    blog(LOG_INFO, "[StageMic] Loaded");
    return true;
}

void obs_module_unload(void)
{
    stagemic::runtime::stop();
    blog(LOG_INFO, "[StageMic] Unloaded");
}
