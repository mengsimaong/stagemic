// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace stagemic
{

/** Registers the "StageMic" audio filter with OBS. Call from obs_module_load(). */
void registerFilter();

} // namespace stagemic
