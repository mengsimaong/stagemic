// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <functional>

// One JUCE runtime shared by every StageMic filter in the OBS process
namespace stagemic::runtime
{

// Call from obs_module_load
bool start();

// Call from obs_module_unload
void stop();

bool isRunning() noexcept;

// Runs fn on the JUCE message thread, asynchronously, in FIFO order
void post(std::function<void()> fn);

} // namespace stagemic::runtime
