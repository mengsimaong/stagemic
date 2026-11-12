// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>

namespace stagemic::test
{

/** Counts heap allocations made on the current thread while in scope.
    Used to prove the audio path never allocates (which could cause dropouts). */
class AllocationGuard
{
public:
    AllocationGuard();
    ~AllocationGuard();
    std::size_t count() const;
};

} // namespace stagemic::test
