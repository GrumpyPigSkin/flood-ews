#include "../../fog/src/security/sensor_replay_detection.hpp"
#include <gtest/gtest.h>
#include <string>

namespace fog::security::test {

namespace {

common::SensorReadingWire make_reading(std::uint64_t eui, std::uint32_t seq,
                                       std::uint64_t timestamp_us) {
  return {
      .m_eui = eui,
      .m_timestamp = timestamp_us,
      .m_water_level_mm = 0,
      .m_validity = {},
      .m_detail = {},
      .m_seq = seq,
  };
}

} // namespace

/**
 * Test seeing a brand new sensor and test bounds, these should always be
 * accepted.
 */

namespace {
struct FirstSeenCase {
  std::string m_name;
  std::uint64_t m_eui;
  std::uint32_t m_seq;
  std::uint64_t m_timestamp;
};

class FirstSeenIsFresh : public ::testing::TestWithParam<FirstSeenCase> {};
} // namespace

TEST_P(FirstSeenIsFresh, ReturnsTrue) {
  SensorReplayDetection detector;
  const auto &param = GetParam();
  EXPECT_TRUE(detector.is_fresh(
      make_reading(param.m_eui, param.m_seq, param.m_timestamp)));
}

INSTANTIATE_TEST_SUITE_P(
    VariousSensors, FirstSeenIsFresh,
    ::testing::Values(FirstSeenCase{"seq_zero", 0x1, 0, 0},
                      FirstSeenCase{"seq_nonzero", 0x2, 42, 1'000},
                      FirstSeenCase{"max_seq", 0x3ULL, UINT32_MAX, 1'000'000}),
    [](const ::testing::TestParamInfo<FirstSeenCase> &info) {
      return info.param.m_name;
    });

/**
 * @brief Stateful tests, test a sequence of calls against the same sensor.
 * This checks the old sequence protection, and sliding window timeout.
 */

namespace {
struct ReplayStep {
  std::uint32_t m_seq;
  std::uint64_t m_timestamp;
  bool m_expect_fresh;
};

struct ReplayScenario {
  std::string name;
  std::uint64_t eui;
  std::vector<ReplayStep> steps;
};

class ReplaySequence : public ::testing::TestWithParam<ReplayScenario> {};
} // namespace

// The test that is run for each scenario.
TEST_P(ReplaySequence, test_matches_expected_freshness_at_each_step) {
  SensorReplayDetection detector;
  const auto &scenario = GetParam();

  for (std::size_t i = 0; i < scenario.steps.size(); ++i) {
    const auto &step = scenario.steps[i];
    SCOPED_TRACE("step " + std::to_string(i));
    EXPECT_EQ(detector.is_fresh(
                  make_reading(scenario.eui, step.m_seq, step.m_timestamp)),
              step.m_expect_fresh);
  }
}

// Test a set of scenarios.
INSTANTIATE_TEST_SUITE_P(
    ReplayBehaviour, ReplaySequence,
    ::testing::Values(
        ReplayScenario{
            "duplicate_rejected", 0x1, {{5, 1'000, true}, {5, 1'000, false}}},
        ReplayScenario{
            "old_sequence_rejected", 0x2, {{10, 0, true}, {9, 1'000, false}}},
        ReplayScenario{"newer_sequence_accepted",
                       0x3,
                       {{1, 0, true}, {2, 100, true}, {3, 200, true}}}),
    [](const ::testing::TestParamInfo<ReplayScenario> &info) {
      return info.param.name;
    });

/**
 * @brief Test the table capacity.
 */

namespace {
class TableCapacity : public ::testing::TestWithParam<int> {};
} // namespace

TEST_P(TableCapacity, test_table_rejects_when_full) {
  SensorReplayDetection detector;
  const auto sensors_to_add = static_cast<std::size_t>(GetParam());

  // Add sensors_to_add distinct sensors.
  for (std::size_t i = 0; i < sensors_to_add; ++i) {
    ASSERT_TRUE(detector.is_fresh(make_reading(i + 1, 0, 0)))
        << "failed while pre-filling entry " << i;
  }

  const bool table_full = sensors_to_add >= SensorReplayDetection::MAX_SENSORS;

  // A new sensor should be rejected when the table is full.
  constexpr std::uint64_t new_eui = 0xDEADBEEF00000001ULL;
  EXPECT_EQ(detector.is_fresh(make_reading(new_eui, 0, 0)), !table_full);
}

INSTANTIATE_TEST_SUITE_P(
    SensorCounts, TableCapacity,
    ::testing::Range(0,
                     static_cast<int>(SensorReplayDetection::MAX_SENSORS) + 1));

} // namespace fog::security::test
