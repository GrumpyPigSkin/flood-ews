#include "../../edge/src/jsn/jsn_logic.hpp"
#include <gtest/gtest.h>

namespace edge::jsn::testing {

TEST(JsnSensorTest, test_distance_calculation_basic_values) {
  // 58 us corresponds to 10 mm
  EXPECT_EQ(distance_mm_from_us(58), 10);

  // 580 us corresponds to 100 mm
  EXPECT_EQ(distance_mm_from_us(580), 100);

  // 5800 us corresponds to 1000 mm
  EXPECT_EQ(distance_mm_from_us(5800), 1000);
}

TEST(JsnSensorTest, test_distance_calculation_zero_input) {
  EXPECT_EQ(distance_mm_from_us(0), 0);
}

constexpr std::uint16_t GROUND_DIST = 3000;

TEST(JsnSensorTest, test_calculate_reading_invalid_reading) {
  constexpr std::uint16_t DISTANCE = 1000;

  const auto reading = calculate_reading(false, DISTANCE, GROUND_DIST);

  EXPECT_EQ(reading.m_water_level, 0);
  EXPECT_EQ(reading.m_validity, common::IEC61850_Validity::VALIDITY_INVALID);
  EXPECT_EQ(reading.m_detail, common::IEC61850_DetailQual::DETAIL_FAILURE);
}

TEST(JsnSensorTest, test_calculate_reading_valid_normal_range) {
  // Distance = 1000mm, Ground = 3000mm -> Level = 2000mm
  constexpr std::uint16_t DISTANCE = 1000;
  constexpr std::uint16_t EXPECTED_WATER_LEVEL = 2000;

  const auto reading = calculate_reading(true, DISTANCE, GROUND_DIST);

  EXPECT_EQ(reading.m_water_level, EXPECTED_WATER_LEVEL);
  EXPECT_EQ(reading.m_validity, common::IEC61850_Validity::VALIDITY_GOOD);
  EXPECT_EQ(reading.m_detail, common::IEC61850_DetailQual::DETAIL_NONE);
}

TEST(JsnSensorTest, test_calculate_reading_below_min_distance) {
  // Distance 200mm is below MIN_DISTANCE_MM (250mm)
  constexpr std::uint16_t LESS_THAN_MIN = 200;
  static_assert(LESS_THAN_MIN < MIN_DISTANCE_MM, "Adjust LESS_THAN_MIN.");

  const auto reading = calculate_reading(true, LESS_THAN_MIN, GROUND_DIST);

  EXPECT_EQ(reading.m_water_level, GROUND_DIST);
  EXPECT_EQ(reading.m_validity,
            common::IEC61850_Validity::VALIDITY_QUESTIONABLE);
  EXPECT_EQ(reading.m_detail, common::IEC61850_DetailQual::DETAIL_OVERFLOW);
}

TEST(JsnSensorTest, test_calculate_reading_beyond_ground_distance) {
  // Distance 3500mm is greater than GROUND_DIST (3000mm)
  constexpr std::uint16_t GREATER_THAN_GROUND = 3500;
  static_assert(GROUND_DIST < GREATER_THAN_GROUND,
                "Adjust GREATER_THAN_GROUND.");

  const auto reading =
      calculate_reading(true, GREATER_THAN_GROUND, GROUND_DIST);

  EXPECT_EQ(reading.m_water_level, 0);
  EXPECT_EQ(reading.m_validity,
            common::IEC61850_Validity::VALIDITY_QUESTIONABLE);
  EXPECT_EQ(reading.m_detail, common::IEC61850_DetailQual::DETAIL_OUT_OF_RANGE);
}

} // namespace edge::jsn::testing
