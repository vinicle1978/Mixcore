#pragma once
// Counts heap allocations/deallocations made on the current thread while a
// ScopedRealtimeCheck is alive. Implemented by replacing the global
// operator new/delete in AllocationTracker.cpp, which is linked ONLY into the
// realtime test executable.

#include <cstddef>

namespace mixcore::test {

struct AllocationCounts
{
    std::size_t allocations = 0;
    std::size_t deallocations = 0;
};

class ScopedRealtimeCheck
{
public:
    ScopedRealtimeCheck();
    ~ScopedRealtimeCheck();
    AllocationCounts counts() const;

    ScopedRealtimeCheck(const ScopedRealtimeCheck&) = delete;
    ScopedRealtimeCheck& operator=(const ScopedRealtimeCheck&) = delete;
};

} // namespace mixcore::test
