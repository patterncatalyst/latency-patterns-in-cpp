#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace line_simulator {

// The five stages every parcel passes through, in order. See CLAUDE.md,
// "Domain": checkweigh, dimension, scan, label, sort.
enum class Stage : std::uint8_t {
    Checkweigh = 0,
    Dimension = 1,
    Scan = 2,
    Label = 3,
    Sort = 4,
};

inline constexpr std::size_t stage_count = 5;

inline constexpr std::array<Stage, stage_count> stage_order{
    Stage::Checkweigh, Stage::Dimension, Stage::Scan, Stage::Label, Stage::Sort,
};

}  // namespace line_simulator
