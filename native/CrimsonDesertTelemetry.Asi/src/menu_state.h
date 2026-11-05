#pragma once
#include <cstdint>

namespace cdt::menu_state
{
enum class State : std::uint32_t { Unknown, Closed, Open };

// Called on the instrumentation worker after the normal game-build checks or
// recovery. Resolves the registry once, then samples only its bounded UI roots.
bool Initialize(std::uint64_t moduleBase, std::uint32_t imageSize);
State Sample();
}
