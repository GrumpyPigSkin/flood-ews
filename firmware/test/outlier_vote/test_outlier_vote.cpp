#include "../../fog/src/outlier_vote/engine.hpp"
#include "common/sensor_reading.hpp"
#include <array>
#include <gtest/gtest.h>
#include <optional>
#include <span>

namespace fog::vote::test {

namespace {

constexpr std::uint16_t DEFAULT_WATER_LVL = 500;
constexpr std::uint16_t BAD_WATER_LVL = 900;

class VoteEngineTest : public ::testing::Test {
protected:
  VoteEngine m_engine;

  static VoteEngine::Entry make_entry(
      Eui eui, std::uint16_t water_level = DEFAULT_WATER_LVL,
      VoteEngine::Validity validity = VoteEngine::Validity::VALIDITY_GOOD,
      VoteEngine::Detail detail = VoteEngine::Detail::DETAIL_NONE,
      std::uint64_t timestamp = 1000) {
    VoteEngine::Entry entry{};
    entry.m_eui = eui;
    entry.m_water_level_mm = water_level;
    entry.m_validity = validity;
    entry.m_detail = detail;
    entry.m_timestamp = timestamp;
    return entry;
  }
};

} // namespace

// ============================================================================
// Accumulate & Slot Capacity Tests
// ============================================================================

TEST_F(VoteEngineTest, test_accumulate_success) {
  const auto entry = make_entry(0x1);
  EXPECT_TRUE(m_engine.accumulate(entry));
}

TEST_F(VoteEngineTest, test_slot_capacity_exceeded) {
  // Fill up all MAX_SENSORS slots
  for (std::size_t i = 0; i < VoteEngine::MAX_SENSORS; ++i) {
    EXPECT_TRUE(m_engine.accumulate(make_entry(i + 1)));
  }

  // Next unique sensor should fail as no slots remain
  EXPECT_FALSE(m_engine.accumulate(make_entry(VoteEngine::MAX_SENSORS + 1)));
}

TEST_F(VoteEngineTest, test_existing_eui) {
  // Fill capacity
  for (std::size_t i = 0; i < VoteEngine::MAX_SENSORS; ++i) {
    EXPECT_TRUE(m_engine.accumulate(make_entry(i + 1)));
  }

  // Re-accumulating for an existing EUI should succeed
  EXPECT_TRUE(m_engine.accumulate(make_entry(1, 600)));
}

// ============================================================================
// Window Closing & Minimum Reporter Tests
// ============================================================================

TEST_F(VoteEngineTest, test_close_window_fails_if_fewer_than_min_reporters) {
  // MIN_REPORTERS is 3
  (void)m_engine.accumulate(make_entry(0x1));
  (void)m_engine.accumulate(make_entry(0x2));

  const auto batch = m_engine.close_window();
  EXPECT_FALSE(batch.has_value());
}

TEST_F(VoteEngineTest, test_close_window_succeeds_with_min_reporters) {
  constexpr std::uint8_t NUM_EXPECTED = 3;
  (void)m_engine.accumulate(make_entry(0x1));
  (void)m_engine.accumulate(make_entry(0x2));
  (void)m_engine.accumulate(make_entry(0x3));

  const auto batch = m_engine.close_window();
  ASSERT_TRUE(batch.has_value());
  const auto got = batch->m_count;
  EXPECT_EQ(got, NUM_EXPECTED);
}

TEST_F(VoteEngineTest, test_open_window_clears_reported_set) {
  (void)m_engine.accumulate(make_entry(0x1));
  (void)m_engine.accumulate(make_entry(0x2));
  (void)m_engine.accumulate(make_entry(0x3));

  m_engine.open_window();

  // Closing immediately after open_window should return std::nullopt
  const auto batch = m_engine.close_window();
  EXPECT_FALSE(batch.has_value());
}

// ============================================================================
// Outlier Detection & Reputation Tests
// ============================================================================

TEST_F(VoteEngineTest,
       test_identifies_outliers_and_applies_questionable_validity) {
  // Median will be 500. Tolerance is 200.
  // 500 +/- 200 range -> [300, 700]. 800 is an outlier.
  constexpr Eui OUTLIER_ENTRY = 0x03;
  (void)m_engine.accumulate(make_entry(0x1));
  (void)m_engine.accumulate(make_entry(0x2));
  (void)m_engine.accumulate(make_entry(OUTLIER_ENTRY, BAD_WATER_LVL));

  auto batch = m_engine.close_window();
  ASSERT_TRUE(batch.has_value());
  ASSERT_EQ(batch->m_count, 3);

  // Check OUTLIER_ENTRY (3rd entry) marked as outlier.
  const auto &outlier_entry = batch->m_entries[2];
  EXPECT_EQ(outlier_entry.m_eui, OUTLIER_ENTRY);
  EXPECT_EQ(outlier_entry.m_validity,
            VoteEngine::Validity::VALIDITY_QUESTIONABLE);
  const auto has_outlier_flag =
      (outlier_entry.m_detail & VoteEngine::Detail::DETAIL_OUTLIER) !=
      VoteEngine::Detail::DETAIL_NONE;
  EXPECT_TRUE(has_outlier_flag) << "Expected DETAIL_OUTLIER flag to be set";
}

TEST_F(VoteEngineTest, test_reputation_penalty_and_exclusion) {
  // Initial reputation = 100, penalty = 20, min_reputation = 10.
  // Repeated outliers:
  // 1st: 80, 2nd: 60, 3rd: 40, 4th: 20, 5th: 10 (clamped to MIN_REPUTATION=10 <
  // min_reputation=20 -> Excluded)

  constexpr Eui OUTLIER_EUI = 0x99;
  constexpr std::int32_t BAD_COUNT = 5;

  for (std::int32_t i = 0; i < BAD_COUNT; ++i) {
    (void)m_engine.accumulate(make_entry(0x1));
    (void)m_engine.accumulate(make_entry(0x2));
    (void)m_engine.accumulate(
        make_entry(OUTLIER_EUI, BAD_WATER_LVL)); // Outlier

    auto batch = m_engine.close_window();
    ASSERT_TRUE(batch.has_value());
  }

  // 6th Window: The excluded sensor should now be marked INVALID even with a
  // good reading
  (void)m_engine.accumulate(make_entry(0x1));
  (void)m_engine.accumulate(make_entry(0x2));
  (void)m_engine.accumulate(make_entry(OUTLIER_EUI));

  auto batch = m_engine.close_window();
  ASSERT_TRUE(batch.has_value());

  const auto &excluded_entry = batch->m_entries[2];
  EXPECT_EQ(excluded_entry.m_eui, OUTLIER_EUI);
  EXPECT_EQ(excluded_entry.m_validity, VoteEngine::Validity::VALIDITY_INVALID);
}

TEST_F(VoteEngineTest, test_reputation_recovery_and_readmission) {
  constexpr Eui OUTLIER_EUI = 0x3;
  constexpr std::int32_t EXCLUSION_COUNT = 5;
  constexpr std::int32_t ADMISSION_COUNT = 6;

  // Force sensor to become excluded
  for (std::int32_t i = 0; i < EXCLUSION_COUNT; ++i) {
    (void)m_engine.accumulate(make_entry(0x1));
    (void)m_engine.accumulate(make_entry(0x2));
    (void)m_engine.accumulate(make_entry(OUTLIER_EUI, BAD_WATER_LVL));
    (void)m_engine.close_window();
  }

  // Verify it is excluded
  std::array<Reputation, VoteEngine::MAX_SENSORS> reps{};
  (void)m_engine.reputation_snapshot(reps);
  auto itr = std::find_if(reps.begin(), reps.end(), [](const Reputation &rep) {
    return rep.m_eui == OUTLIER_EUI;
  });
  ASSERT_NE(itr, reps.end());
  EXPECT_TRUE(itr->m_excluded);

  // Provide clean readings (+5 reputation per clean window, need to reach
  // readmit threshold = 40) From 10 -> 15, 20, 25, 30, 35, 40 (6 clean windows)
  for (int i = 0; i < ADMISSION_COUNT; ++i) {
    (void)m_engine.accumulate(make_entry(0x1));
    (void)m_engine.accumulate(make_entry(0x2));
    (void)m_engine.accumulate(make_entry(OUTLIER_EUI));
    (void)m_engine.close_window();
  }

  // Verify readmission
  (void)m_engine.reputation_snapshot(reps);
  itr = std::find_if(reps.begin(), reps.end(), [](const Reputation &rep) {
    return rep.m_eui == OUTLIER_EUI;
  });
  ASSERT_NE(itr, reps.end());
  EXPECT_FALSE(itr->m_excluded);

  constexpr std::uint8_t EXPECTED_REPUTATION = 40;
  EXPECT_GE(itr->m_reputation, EXPECTED_REPUTATION);
}

// ============================================================================
// Alert & Hysteresis Tests
// ============================================================================

TEST_F(VoteEngineTest, test_alert_trigger_and_clear_hysteresis) {
  EXPECT_FALSE(m_engine.alert_active());

  constexpr std::uint16_t HIGH_WATER_LVL = 1600;

  (void)m_engine.accumulate(make_entry(0x1, HIGH_WATER_LVL));
  (void)m_engine.accumulate(make_entry(0x2, HIGH_WATER_LVL));
  (void)m_engine.accumulate(make_entry(0x3, HIGH_WATER_LVL));
  auto batch = m_engine.close_window();
  ASSERT_TRUE(batch.has_value());

  EXPECT_TRUE(m_engine.check_alert(*batch));
  EXPECT_TRUE(m_engine.alert_active());

  // Clean windows.
  for (int i = 1; i <= 3; ++i) {
    (void)m_engine.accumulate(make_entry(0x1));
    (void)m_engine.accumulate(make_entry(0x2));
    (void)m_engine.accumulate(make_entry(0x3));
    batch = m_engine.close_window();

    if (i < 3) {
      EXPECT_TRUE(m_engine.check_alert(*batch))
          << "Alert should stay active on window " << i;
    } else {
      EXPECT_FALSE(m_engine.check_alert(*batch))
          << "Alert should clear on window 3";
    }
  }

  EXPECT_FALSE(m_engine.alert_active());
}

// ============================================================================
// Reputation Snapshot Tests
// ============================================================================

TEST_F(VoteEngineTest, ReputationSnapshotExport) {
  (void)m_engine.accumulate(make_entry(0x1));
  (void)m_engine.accumulate(make_entry(0x2));
  (void)m_engine.accumulate(make_entry(0x3));
  (void)m_engine.close_window();

  std::array<Reputation, VoteEngine::MAX_SENSORS> buf{};
  const std::size_t count = m_engine.reputation_snapshot(buf);

  EXPECT_EQ(count, 3);
  EXPECT_EQ(buf[0].m_eui, 0x1);
  EXPECT_EQ(buf[1].m_eui, 0x2);
  EXPECT_EQ(buf[2].m_eui, 0x3);
}

} // namespace fog::vote::test
