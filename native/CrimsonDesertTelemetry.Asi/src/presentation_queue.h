#pragma once
#include "device_identity.h"
#include <windows.h>
#include <d3d12.h>

namespace cdt::render::presentation_queue
{
// The overlay publishes the game's own swapchain queue here when it captures it.
// Capture Prepare() then takes ExecuteCommandLists from it instead of creating a
// probe queue at world entry (that creation preceded a GPU hang on 2026-09-29).
// Only the function address and the canonical device identity are kept. The
// identity is compared as a pointer and never used as an object afterwards.
struct Info { void* execute{}; const void* device{}; };
inline SRWLOCK lock = SRWLOCK_INIT;
inline Info info{};

inline void Publish(void* execute, const void* device) noexcept
{
    if (!execute || !device) return;
    AcquireSRWLockExclusive(&lock);
    info = {execute, device};
    ReleaseSRWLockExclusive(&lock);
}

inline void Publish(ID3D12CommandQueue* queue) noexcept
{
    if (!queue) return;
    IUnknown* identity{};
    if (FAILED(device_identity::ChildDeviceIdentity(queue, &identity)) || !identity) return;
    // The game's device outlives the process-lifetime capture; keep no reference.
    identity->Release();
    Publish((*reinterpret_cast<void***>(queue))[10], identity);
}

inline Info Current() noexcept
{
    AcquireSRWLockShared(&lock);
    const auto result = info;
    ReleaseSRWLockShared(&lock);
    return result;
}
}
