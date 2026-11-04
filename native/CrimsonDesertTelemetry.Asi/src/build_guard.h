#pragma once
#include <array>
namespace cdt::instruments {
bool VerifyExecutable();
bool ReadExecutableHash(std::array<unsigned char,32>& digest);
}
