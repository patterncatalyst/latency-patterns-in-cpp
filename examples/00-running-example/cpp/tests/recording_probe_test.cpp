#include "ch00/recording_probe.hpp"

#include <gtest/gtest.h>

#include <chrono>

#include "line_simulator/parcel.hpp"
#include "measure/latency_recorder.hpp"

namespace ch00 {
namespace {

using line_simulator::Clock;
using line_simulator::Parcel;
using measure::LatencyRecorder;
using namespace std::chrono_literals;

TEST(RecordingProbe, RecordsTheSenseToActSpanAsOneSample) {
    LatencyRecorder recorder;
    RecordingProbe probe{recorder};
    const Parcel parcel{};
    const Clock::time_point sense_at = Clock::now();

    probe.on_sense(parcel, sense_at);
    probe.on_decide(parcel, sense_at + 1ms);
    probe.on_act(parcel, sense_at + 5ms);

    ASSERT_EQ(recorder.sample_count(), 1U);
    EXPECT_NEAR(recorder.percentile(100), std::chrono::duration<double>(5ms).count(), 1e-9);
}

TEST(RecordingProbe, OnDecideNeverChangesTheRecordedDuration) {
    // The spine metric is sense-to-act, not sense-to-decide or
    // decide-to-act (see CLAUDE.md, "The spine"): a decide callback that
    // arrives long after sense, or right before act, must not move the
    // recorded sample.
    LatencyRecorder recorder;
    RecordingProbe probe{recorder};
    const Parcel parcel{};
    const Clock::time_point sense_at = Clock::now();

    probe.on_sense(parcel, sense_at);
    probe.on_decide(parcel, sense_at + 500ms);  // arrives far later than act
    probe.on_act(parcel, sense_at + 2ms);

    ASSERT_EQ(recorder.sample_count(), 1U);
    EXPECT_NEAR(recorder.percentile(100), std::chrono::duration<double>(2ms).count(), 1e-9);
}

TEST(RecordingProbe, SequentialParcelsEachRecordTheirOwnSpan) {
    // One RecordingProbe serves every parcel in a run; this locks in that a
    // later parcel's sense does not corrupt an earlier parcel's already
    // recorded sample (see the class comment on why one sense_at_ member is
    // safe for the line-simulator's synchronous, one-parcel-at-a-time loop).
    LatencyRecorder recorder;
    RecordingProbe probe{recorder};
    const Parcel parcel{};
    const Clock::time_point start = Clock::now();

    probe.on_sense(parcel, start);
    probe.on_act(parcel, start + 3ms);

    probe.on_sense(parcel, start + 10ms);
    probe.on_act(parcel, start + 10ms + 7ms);

    ASSERT_EQ(recorder.sample_count(), 2U);
    EXPECT_NEAR(recorder.percentile(50), std::chrono::duration<double>(3ms).count(), 1e-9);
    EXPECT_NEAR(recorder.percentile(100), std::chrono::duration<double>(7ms).count(), 1e-9);
}

}  // namespace
}  // namespace ch00
