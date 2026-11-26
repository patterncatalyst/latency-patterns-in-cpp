#include "ch07/dimension_classifier.hpp"

namespace ch07 {

namespace {

// Newton's method for a cube root, run to a fixed iteration count rather
// than to a convergence tolerance: the point is a real, repeatable amount of
// floating-point work per call (standing in for whatever a real packing-
// radius derivation costs), not the fastest way to get a cube root.
double approximate_cube_root(double value) {
    double x = value > 0.0 ? value : 1.0;
    for (int i = 0; i < 40; ++i) {
        x = x - (((x * x * x) - value) / (3.0 * x * x));
    }
    return x;
}

}  // namespace

SizeClass classify_dimensions(double length_cm, double width_cm, double height_cm) {
    const double volume_cm3 = length_cm * width_cm * height_cm;
    const double packing_radius_cm = approximate_cube_root(volume_cm3);
    const double girth_cm = length_cm + (2.0 * (width_cm + height_cm));

    if (girth_cm > 300.0 || packing_radius_cm > 60.0) {
        return SizeClass::Oversize;
    }
    if (girth_cm > 180.0 || packing_radius_cm > 40.0) {
        return SizeClass::Large;
    }
    if (girth_cm > 90.0 || packing_radius_cm > 20.0) {
        return SizeClass::Medium;
    }
    return SizeClass::Small;
}

}  // namespace ch07
