# Third-party software

StageMic is licensed under the **GNU Affero General Public License v3.0 or later**
(see `LICENSE`). It builds on:

| Component | Used for | License |
|---|---|---|
| [JUCE](https://juce.com) 9.0.5 | audio engine framework, UI, VST3 wrapper | AGPL-3.0 (open-source option) |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (bundled with JUCE) | VST3 build | MIT |
| [OBS Studio](https://obsproject.com) libobs / obs-frontend-api | OBS plugin API | GPL-2.0-or-later |
| [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) | CMake build helpers in `cmake/` | GPL-2.0 |

Architecture reference (no code copied):
[atkAudio Plugin for OBS](https://github.com/atkAudio/PluginForObsRelease), AGPL-3.0.
