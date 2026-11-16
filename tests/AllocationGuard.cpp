// SPDX-License-Identifier: AGPL-3.0-or-later
// Counts allocations: replaces global operator new/delete in the tests
#include "AllocationGuard.h"

#include <cstdlib>
#include <new>

namespace
{
thread_local bool tracking = false;
thread_local std::size_t allocations = 0;

// Set a debugger breakpoint here to see who allocated during a guarded
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void onTrackedAllocation(std::size_t size)
{
    static volatile std::size_t lastSize = 0;
    lastSize = size;
    (void)lastSize;
}

void* allocate(std::size_t size)
{
    if (tracking)
    {
        ++allocations;
        onTrackedAllocation(size);
    }

    if (size == 0)
        size = 1;

    if (void* p = std::malloc(size))
        return p;

    throw std::bad_alloc();
}
} // namespace

void* operator new(std::size_t size)
{
    return allocate(size);
}
void* operator new[](std::size_t size)
{
    return allocate(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return allocate(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return allocate(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

namespace stagemic::test
{
AllocationGuard::AllocationGuard()
{
    allocations = 0;
    tracking = true;
}

AllocationGuard::~AllocationGuard()
{
    tracking = false;
}

std::size_t AllocationGuard::count() const
{
    return allocations;
}
} // namespace stagemic::test
