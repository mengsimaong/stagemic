// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace stagemic
{

/** Makes `window` an owned window of `owner` (both native handles).
    On Windows an owned window always stays above its owner, minimises with it
    and has no separate taskbar button - so StageMic never hides behind OBS.
    No-op on other platforms or if either handle is null. */
void setNativeWindowOwner(void* window, void* owner);

} // namespace stagemic
