#include "AllocationTracker.h"

#include <cstdlib>
#if defined(_WIN32)
#include <malloc.h>
#endif
#include <new>

namespace {
thread_local bool tTracking = false;
thread_local std::size_t tAllocs = 0;
thread_local std::size_t tFrees = 0;

void* allocate(std::size_t size, std::size_t alignment)
{
    if (tTracking)
        ++tAllocs;
    if (size == 0)
        size = 1;
    void* p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(size, alignment < alignof(std::max_align_t) ? alignof(std::max_align_t) : alignment);
#else
    if (alignment > alignof(std::max_align_t))
    {
        const std::size_t rounded = (size + alignment - 1) / alignment * alignment;
        p = std::aligned_alloc(alignment, rounded);
    }
    else
    {
        p = std::malloc(size);
    }
#endif
    if (!p)
        throw std::bad_alloc();
    return p;
}

void release(void* p) noexcept
{
    if (!p)
        return;
    if (tTracking)
        ++tFrees;
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
} // namespace

void* operator new(std::size_t s) { return allocate(s, alignof(std::max_align_t)); }
void* operator new[](std::size_t s) { return allocate(s, alignof(std::max_align_t)); }
void* operator new(std::size_t s, std::align_val_t a) { return allocate(s, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t s, std::align_val_t a) { return allocate(s, static_cast<std::size_t>(a)); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::align_val_t) noexcept { release(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { release(p); }

namespace mixcore::test {

ScopedRealtimeCheck::ScopedRealtimeCheck()
{
    tAllocs = 0;
    tFrees = 0;
    tTracking = true;
}

ScopedRealtimeCheck::~ScopedRealtimeCheck() { tTracking = false; }

AllocationCounts ScopedRealtimeCheck::counts() const { return { tAllocs, tFrees }; }

} // namespace mixcore::test
