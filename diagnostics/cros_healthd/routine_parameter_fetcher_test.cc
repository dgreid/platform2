// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <string>

#include <base/optional.h>
#include <chromeos/chromeos-config/libcros_config/fake_cros_config.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routine_parameter_fetcher.h"
#include "diagnostics/cros_healthd/routine_parameter_fetcher_constants.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {

namespace {

// POD struct for GetBatteryCapacityParametersTest.
struct GetBatteryCapacityParametersTestParams {
  base::Optional<std::string> low_mah_in;
  base::Optional<std::string> high_mah_in;
  base::Optional<uint32_t> expected_low_mah_out;
  base::Optional<uint32_t> expected_high_mah_out;
};

// POD struct for GetBatteryHealthParametersTest.
struct GetBatteryHealthParametersTestParams {
  base::Optional<std::string> maximum_cycle_count_in;
  base::Optional<std::string> percent_battery_wear_allowed_in;
  base::Optional<uint32_t> expected_maximum_cycle_count_out;
  base::Optional<uint8_t> expected_percent_battery_wear_allowed_out;
};

// POD struct for GetPrimeSearchParametersTest.
struct GetPrimeSearchParametersTestParams {
  base::Optional<std::string> max_num_in;
  base::Optional<uint64_t> expected_max_num_out;
};

}  // namespace

class RoutineParameterFetcherTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    parameter_fetcher_ =
        std::make_unique<RoutineParameterFetcher>(mock_context_.cros_config());
  }

  RoutineParameterFetcher* parameter_fetcher() const {
    return parameter_fetcher_.get();
  }

  // If |value| is specified, writes |value| to |property| at
  // |cros_config_path|.
  void MaybeWriteCrosConfigData(const base::Optional<std::string>& value,
                                const std::string& property,
                                const std::string& cros_config_path) {
    if (value.has_value()) {
      mock_context_.fake_cros_config()->SetString(cros_config_path, property,
                                                  value.value());
    }
  }

 private:
  MockContext mock_context_;
  std::unique_ptr<RoutineParameterFetcher> parameter_fetcher_;
};

// Tests for the GetBatteryCapacityParameters() method of
// RoutineParameterFetcher with different values present in cros_config.
//
// This is a parameterized test with the following parameters (accessed
// through the GetBatteryCapacityParametersTestParams POD struct):
// * |low_mah_in| - If specified, will be written to cros_config's low_mah
// property.
// * |high_mah_in| - If specified, will be written to cros_config's high_mah
// property.
// * |expected_low_mah_out| - Expected value of |low_mah_out| after
// GetBatteryCapacityParameters() returns.
// * |expected_high_mah_out| - Expected value of |high_mah_out| after
// GetBatteryCapacityParameters() returns.
class GetBatteryCapacityParametersTest
    : public RoutineParameterFetcherTest,
      public testing::WithParamInterface<
          GetBatteryCapacityParametersTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  GetBatteryCapacityParametersTestParams params() const { return GetParam(); }
};

// Test that GetBatteryCapacityParameters() returns correct values.
TEST_P(GetBatteryCapacityParametersTest, ReturnsCorrectValues) {
  MaybeWriteCrosConfigData(params().low_mah_in, kLowMahProperty,
                           kBatteryCapacityPropertiesPath);
  MaybeWriteCrosConfigData(params().high_mah_in, kHighMahProperty,
                           kBatteryCapacityPropertiesPath);

  base::Optional<uint32_t> actual_low_mah_out;
  base::Optional<uint32_t> actual_high_mah_out;
  parameter_fetcher()->GetBatteryCapacityParameters(&actual_low_mah_out,
                                                    &actual_high_mah_out);

  EXPECT_EQ(actual_low_mah_out, params().expected_low_mah_out);
  EXPECT_EQ(actual_high_mah_out, params().expected_high_mah_out);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    GetBatteryCapacityParametersTest,
    testing::Values(GetBatteryCapacityParametersTestParams{
                        /*low_mah_in=*/base::nullopt,
                        /*high_mah_in=*/base::nullopt,
                        /*expected_low_mah_out=*/base::nullopt,
                        /*expected_high_mah_out=*/base::nullopt},
                    GetBatteryCapacityParametersTestParams{
                        /*low_mah_in=*/"not_int_value",
                        /*high_mah_in=*/base::nullopt,
                        /*expected_low_mah_out=*/base::nullopt,
                        /*expected_high_mah_out=*/base::nullopt},
                    GetBatteryCapacityParametersTestParams{
                        /*low_mah_in=*/"1000",
                        /*high_mah_in=*/base::nullopt,
                        /*expected_low_mah_out=*/1000,
                        /*expected_high_mah_out=*/base::nullopt},
                    GetBatteryCapacityParametersTestParams{
                        /*low_mah_in=*/base::nullopt,
                        /*high_mah_in=*/"not_int_value",
                        /*expected_low_mah_out=*/base::nullopt,
                        /*expected_high_mah_out=*/base::nullopt},
                    GetBatteryCapacityParametersTestParams{
                        /*low_mah_in=*/"not_int_value",
                        /*high_mah_in=*/"not_int_value",
                        /*expected_low_mah_out=*/base::nullopt,
                        /*expected_high_mah_out=*/base::nullopt},
                    GetBatteryCapacityParametersTestParams{
                        /*low_mah_in=*/"1000",
                        /*high_mah_in=*/"not_int_value",
                        /*expected_low_mah_out=*/1000,
                        /*expected_high_mah_out=*/base::nullopt},
                    GetBatteryCapacityParametersTestParams{
                        /*low_mah_in=*/base::nullopt,
                        /*high_mah_in=*/"10000",
                        /*expected_low_mah_out=*/base::nullopt,
                        /*expected_high_mah_out=*/10000},
                    GetBatteryCapacityParametersTestParams{
                        /*low_mah_in=*/"not_int_value",
                        /*high_mah_in=*/"10000",
                        /*expected_low_mah_out=*/base::nullopt,
                        /*expected_high_mah_out=*/10000},
                    GetBatteryCapacityParametersTestParams{
                        /*low_mah_in=*/"1000",
                        /*high_mah_in=*/"10000",
                        /*expected_low_mah_out=*/1000,
                        /*expected_high_mah_out=*/10000}));

// Tests for the GetBatteryHealthParameters() method of RoutineParameterFetcher
// with different values present in cros_config.
//
// This is a parameterized test with the following parameters (accessed
// through the GetBatteryHealthParametersTestParams POD struct):
// * |maximum_cycle_count_in| - If specified, will be written to cros_config's
// maximum_cycle_count property.
// * |percent_battery_wear_allowed_in| - If specified, will be written to
// cros_config's percent_battery_wear_allowed property.
// * |expected_maximum_cycle_count_out| - Expected value of
// |maximum_cycle_count_out| after GetBatteryHealthParameters() returns.
// * |expected_percent_battery_wear_allowed_out| - Expected value of
// |percent_battery_wear_allowed_out| after GetBatteryHealthParameters()
// returns.
class GetBatteryHealthParametersTest
    : public RoutineParameterFetcherTest,
      public testing::WithParamInterface<GetBatteryHealthParametersTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  GetBatteryHealthParametersTestParams params() const { return GetParam(); }
};

// Test that GetBatteryHealthParameters() returns correct values.
TEST_P(GetBatteryHealthParametersTest, ReturnsCorrectValues) {
  MaybeWriteCrosConfigData(params().maximum_cycle_count_in,
                           kMaximumCycleCountProperty,
                           kBatteryHealthPropertiesPath);
  MaybeWriteCrosConfigData(params().percent_battery_wear_allowed_in,
                           kPercentBatteryWearAllowedProperty,
                           kBatteryHealthPropertiesPath);

  base::Optional<uint32_t> actual_maximum_cycle_count_out;
  base::Optional<uint8_t> actual_percent_battery_wear_allowed_out;
  parameter_fetcher()->GetBatteryHealthParameters(
      &actual_maximum_cycle_count_out,
      &actual_percent_battery_wear_allowed_out);

  EXPECT_EQ(actual_maximum_cycle_count_out,
            params().expected_maximum_cycle_count_out);
  EXPECT_EQ(actual_percent_battery_wear_allowed_out,
            params().expected_percent_battery_wear_allowed_out);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    GetBatteryHealthParametersTest,
    testing::Values(GetBatteryHealthParametersTestParams{
                        /*maximum_cycle_count_in=*/base::nullopt,
                        /*percent_battery_wear_allowed_in=*/base::nullopt,
                        /*expected_maximum_cycle_count_out=*/base::nullopt,
                        /*percent_battery_wear_allowed_out=*/base::nullopt},
                    GetBatteryHealthParametersTestParams{
                        /*maximum_cycle_count_in=*/"not_int_value",
                        /*percent_battery_wear_allowed_in=*/base::nullopt,
                        /*expected_maximum_cycle_count_out=*/base::nullopt,
                        /*percent_battery_wear_allowed_out=*/base::nullopt},
                    GetBatteryHealthParametersTestParams{
                        /*maximum_cycle_count_in=*/"1000",
                        /*percent_battery_wear_allowed_in=*/base::nullopt,
                        /*expected_maximum_cycle_count_out=*/1000,
                        /*percent_battery_wear_allowed_out=*/base::nullopt},
                    GetBatteryHealthParametersTestParams{
                        /*maximum_cycle_count_in=*/base::nullopt,
                        /*percent_battery_wear_allowed_in=*/"not_int_value",
                        /*expected_maximum_cycle_count_out=*/base::nullopt,
                        /*percent_battery_wear_allowed_out=*/base::nullopt},
                    GetBatteryHealthParametersTestParams{
                        /*maximum_cycle_count_in=*/"not_int_value",
                        /*percent_battery_wear_allowed_in=*/"not_int_value",
                        /*expected_maximum_cycle_count_out=*/base::nullopt,
                        /*percent_battery_wear_allowed_out=*/base::nullopt},
                    GetBatteryHealthParametersTestParams{
                        /*maximum_cycle_count_in=*/"1000",
                        /*percent_battery_wear_allowed_in=*/"not_int_value",
                        /*expected_maximum_cycle_count_out=*/1000,
                        /*percent_battery_wear_allowed_out=*/base::nullopt},
                    GetBatteryHealthParametersTestParams{
                        /*maximum_cycle_count_in=*/base::nullopt,
                        /*percent_battery_wear_allowed_in=*/"50",
                        /*expected_maximum_cycle_count_out=*/base::nullopt,
                        /*percent_battery_wear_allowed_out=*/50},
                    GetBatteryHealthParametersTestParams{
                        /*maximum_cycle_count_in=*/"not_int_value",
                        /*percent_battery_wear_allowed_in=*/"50",
                        /*expected_maximum_cycle_count_out=*/base::nullopt,
                        /*percent_battery_wear_allowed_out=*/50},
                    GetBatteryHealthParametersTestParams{
                        /*maximum_cycle_count_in=*/"1000",
                        /*percent_battery_wear_allowed_in=*/"50",
                        /*expected_maximum_cycle_count_out=*/1000,
                        /*percent_battery_wear_allowed_out=*/50}));

// Tests for the GetPrimeSearchParameters() method of RoutineParameterFetcher
// with different values present in cros_config.
//
// This is a parameterized test with the following parameters (accessed
// through the GetPrimeSearchParametersTestParams POD struct):
// * |max_num_in| - If specified, will be written to cros_config's max_num
// property.
// * |expected_max_num_out| - Expected value of
// |max_num_out| after GetPrimeSearchParameters() returns.
class GetPrimeSearchParametersTest
    : public RoutineParameterFetcherTest,
      public testing::WithParamInterface<GetPrimeSearchParametersTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  GetPrimeSearchParametersTestParams params() const { return GetParam(); }
};

// Test that GetBatteryHealthParameters() returns correct values.
TEST_P(GetPrimeSearchParametersTest, ReturnsCorrectValues) {
  MaybeWriteCrosConfigData(params().max_num_in, kMaxNumProperty,
                           kPrimeSearchPropertiesPath);

  base::Optional<uint64_t> actual_max_num_out;
  parameter_fetcher()->GetPrimeSearchParameters(&actual_max_num_out);

  EXPECT_EQ(actual_max_num_out, params().expected_max_num_out);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    GetPrimeSearchParametersTest,
    testing::Values(GetPrimeSearchParametersTestParams{
                        /*max_num_in=*/base::nullopt,
                        /*expected_max_num_out=*/base::nullopt},
                    GetPrimeSearchParametersTestParams{
                        /*max_num_in=*/"not_int_value",
                        /*expected_max_num_out=*/base::nullopt},
                    GetPrimeSearchParametersTestParams{
                        /*max_num_in=*/"10000000000",
                        /*expected_max_num_out=*/10000000000}));

}  // namespace diagnostics
