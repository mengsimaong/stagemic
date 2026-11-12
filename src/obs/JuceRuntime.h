// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <functional>

/** One JUCE runtime shared by every StageMic filter in the OBS process.

    Windows: JUCE's message thread *is* OBS's UI thread. JUCE posts its
    messages to a hidden Win32 window and Qt's event loop delivers them, so no
    extra pumping is needed (this is how JUCE plugins work in any Windows host).

    Linux (development/testing only for now): JUCE gets its own dedicated
    message thread running JUCE's event loop.
*/
namespace stagemic::runtime
{

/** Call from obs_module_load(). */
bool start();

/** Call from obs_module_unload(). Runs any queued work, then shuts JUCE down. */
void stop();

bool isRunning() noexcept;

/** Runs fn on the JUCE message thread, asynchronously, in FIFO order.
    Safe to call from any thread except the audio thread (it allocates).
    Work posted before stop() is guaranteed to run. */
void post(std::function<void()> fn);

} // namespace stagemic::runtime
