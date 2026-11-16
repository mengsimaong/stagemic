// SPDX-License-Identifier: AGPL-3.0-or-later
#include "obs/ObsWindowOwner.h"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace stagemic
{

void setNativeWindowOwner(void* window, void* owner)
{
#ifdef _WIN32
    if (window == nullptr || owner == nullptr)
        return;

    SetWindowLongPtrW(static_cast<HWND>(window), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
#else
    (void)window;
    (void)owner;
#endif
}

} // namespace stagemic
