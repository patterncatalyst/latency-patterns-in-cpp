#pragma once

#include <cstdint>

namespace ch07 {

// The size classes a parcel's dimensions sort it into, which chute capacity
// planning and conveyor spacing both key off downstream (out of scope here;
// this chapter only needs that the classification is real and repeatable).
enum class SizeClass : std::uint8_t {
    Small,
    Medium,
    Large,
    Oversize,
};

// A pure function: the same (length_cm, width_cm, height_cm) always
// produces the same SizeClass, and nothing about the answer depends on when
// it is called or what was asked before it. It is real work, not a stub: it
// derives an effective packing radius from the parcel's volume via Newton's
// method (an iterative cube root) and combines that with the girth
// (length + 2 * (width + height), the measurement carriers actually bill
// by) to pick a class. Being pure is exactly what makes memoizing it safe
// and unbounded; see Memoizer's comment for the contrast with an
// eviction-managed cache.
[[nodiscard]] SizeClass classify_dimensions(double length_cm, double width_cm, double height_cm);

}  // namespace ch07
