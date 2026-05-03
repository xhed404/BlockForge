#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace blockforge
{
std::array<std::uint8_t, 16> md5(std::span<const std::uint8_t> data);
}

