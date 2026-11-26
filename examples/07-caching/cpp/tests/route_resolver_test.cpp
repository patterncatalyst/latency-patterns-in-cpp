#include "ch07/route_resolver.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ch07 {
namespace {

TEST(RouteResolverTest, ResolveMatchesGroundTruthForEveryKey) {
    const RouteResolver resolver(64);
    for (int key = 0; key < 64; ++key) {
        EXPECT_EQ(resolver.resolve(key), resolver.ground_truth(key)) << "key=" << key;
    }
}

TEST(RouteResolverTest, ResolveIsDeterministicAcrossRepeatedCalls) {
    const RouteResolver resolver(64);
    const int first = resolver.resolve(17);
    const int second = resolver.resolve(17);
    EXPECT_EQ(first, second);
}

TEST(RouteResolverTest, ScanChecksumIsNonzeroProvingTheLoopRan) {
    const RouteResolver resolver(64);
    const int value = resolver.resolve(40);
    EXPECT_EQ(value, resolver.ground_truth(40));
    EXPECT_NE(resolver.last_scan_checksum(), 0U);
}

TEST(RouteResolverTest, RerouteChangesOnlyTheNamedKeys) {
    RouteResolver resolver(64);
    const int before_rerouted = resolver.ground_truth(5);
    const int before_untouched = resolver.ground_truth(6);

    resolver.reroute({5});

    EXPECT_NE(resolver.ground_truth(5), before_rerouted);
    EXPECT_EQ(resolver.ground_truth(6), before_untouched);
    EXPECT_EQ(resolver.resolve(5), resolver.ground_truth(5));
}

TEST(RouteResolverTest, RerouteAssignsAChuteOutsideTheOriginalRange) {
    RouteResolver resolver(64);
    resolver.reroute({3});
    // manifest_chute_for_key's original range is [0, 16); a rerouted chute
    // must be unambiguously distinguishable from a never-rerouted one.
    EXPECT_GE(resolver.ground_truth(3), 16);
}

}  // namespace
}  // namespace ch07
