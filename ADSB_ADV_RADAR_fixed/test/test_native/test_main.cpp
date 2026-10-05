// Native (host) Unity tests for the pure, Arduino-free math extracted for
// the settings overhaul - see platformio.ini [env:native]. Run with:
//   pio test -e native
#include <unity.h>
#include <math.h>
#include "../../src/unit_math.h"
#include "../../src/alert_filter.h"
#include "../../src/radar_math.h"

void setUp() {}
void tearDown() {}

// --- UnitMath -----------------------------------------------------------

void test_km_to_nm() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.3197f, UnitMath::kmToNm(8.0f));
}

void test_km_to_miles() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.9710f, UnitMath::kmToMiles(8.0f));
}

void test_ft_to_meters() {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1524.0f, UnitMath::ftToMeters(5000.0f));
}

void test_zero_conversions_are_zero() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, UnitMath::kmToNm(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, UnitMath::kmToMiles(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, UnitMath::ftToMeters(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, UnitMath::knotsToKmh(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, UnitMath::knotsToMph(0.0f));
}

void test_knots_to_kmh() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 185.2f, UnitMath::knotsToKmh(100.0f));
}

void test_knots_to_mph() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 115.078f, UnitMath::knotsToMph(100.0f));
}

// --- RadarMath::applyRotation ---------------------------------------------

void test_rotation_zero_is_identity() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 42.0f, RadarMath::applyRotation(42.0f, 0.0f));
}

void test_rotation_matching_bearing_becomes_zero() {
    // The user's own example: entering rotation=135 should make an
    // aircraft that's actually bearing 135 (south-east) render at the top
    // of the screen (effective bearing 0).
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, RadarMath::applyRotation(135.0f, 135.0f));
}

void test_rotation_wraps_below_zero() {
    // bearing 10, rotated by 350 -> (10 - 350) wraps up to 20, not -340.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, RadarMath::applyRotation(10.0f, 350.0f));
}

void test_rotation_north_indicator_position() {
    // Where True North (bearing 0) ends up on screen at a given rotation -
    // same call the "N" indicator uses.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 225.0f, RadarMath::applyRotation(0.0f, 135.0f));
}

// --- RadarMath::rotateVector -----------------------------------------------
// Regression coverage for a real bug found by inspection: the aircraft
// heading-arrow triangle used to rotate its local "nose forward" (0,-1)
// vertex with the wrong sign convention - a mirror, not a rotation. It
// looked fine at heading 0/180 (sin(0)=sin(180)=0 hides the bug) but pointed
// every East/West-leaning heading to the wrong side of the radar (e.g. a
// heading-90/East aircraft's arrow pointed West). These pin the same
// dx=sin/dy=-cos convention RadarMath::toScreen() already uses.

void test_rotate_vector_heading_zero_points_up() {
    RadarMath::ScreenVector v = RadarMath::rotateVector(0.0f, -1.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, v.dx);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, v.dy);
}

void test_rotate_vector_heading_east_points_right() {
    // This is the case the old code got backwards.
    RadarMath::ScreenVector v = RadarMath::rotateVector(0.0f, -1.0f, 90.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, v.dx);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, v.dy);
}

void test_rotate_vector_heading_west_points_left() {
    RadarMath::ScreenVector v = RadarMath::rotateVector(0.0f, -1.0f, 270.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, v.dx);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, v.dy);
}

void test_rotate_vector_heading_south_points_down() {
    RadarMath::ScreenVector v = RadarMath::rotateVector(0.0f, -1.0f, 180.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, v.dx);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, v.dy);
}

void test_rotate_vector_matches_toScreen_bearing_mapping() {
    // rotateVector() applied to the "forward" unit vector must agree with
    // toScreen()'s own dx/dy for the same angle, at every 45 degrees - the
    // arrow and the blip position must always point the same way.
    const float headings[] = {0, 45, 90, 135, 180, 225, 270, 315};
    for (float h : headings) {
        RadarMath::ScreenVector v = RadarMath::rotateVector(0.0f, -1.0f, h);
        double rad = h * (M_PI / 180.0);
        float expectedDx = static_cast<float>(sin(rad));
        float expectedDy = static_cast<float>(-cos(rad));
        TEST_ASSERT_FLOAT_WITHIN(0.001f, expectedDx, v.dx);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, expectedDy, v.dy);
    }
}

// --- AlertFilter ----------------------------------------------------------

void test_alert_within_both_thresholds() {
    TEST_ASSERT_TRUE(AlertFilter::shouldAlert(5.0f, 3000.0f, false, 8.0f, 5000.0f));
}

void test_alert_boundary_values_are_inclusive() {
    TEST_ASSERT_TRUE(AlertFilter::shouldAlert(8.0f, 5000.0f, false, 8.0f, 5000.0f));
}

void test_no_alert_when_distance_exceeds_threshold() {
    TEST_ASSERT_FALSE(AlertFilter::shouldAlert(8.1f, 3000.0f, false, 8.0f, 5000.0f));
}

void test_no_alert_when_altitude_exceeds_threshold() {
    TEST_ASSERT_FALSE(AlertFilter::shouldAlert(3.0f, 5001.0f, false, 8.0f, 5000.0f));
}

void test_no_alert_when_both_exceed_threshold() {
    TEST_ASSERT_FALSE(AlertFilter::shouldAlert(50.0f, 40000.0f, false, 8.0f, 5000.0f));
}

void test_emergency_always_alerts_regardless_of_range() {
    // Far away and high up - would normally never qualify, but an
    // emergency squawk must always beep.
    TEST_ASSERT_TRUE(AlertFilter::shouldAlert(200.0f, 40000.0f, true, 8.0f, 5000.0f));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_km_to_nm);
    RUN_TEST(test_km_to_miles);
    RUN_TEST(test_ft_to_meters);
    RUN_TEST(test_zero_conversions_are_zero);
    RUN_TEST(test_knots_to_kmh);
    RUN_TEST(test_knots_to_mph);

    RUN_TEST(test_rotation_zero_is_identity);
    RUN_TEST(test_rotation_matching_bearing_becomes_zero);
    RUN_TEST(test_rotation_wraps_below_zero);
    RUN_TEST(test_rotation_north_indicator_position);

    RUN_TEST(test_rotate_vector_heading_zero_points_up);
    RUN_TEST(test_rotate_vector_heading_east_points_right);
    RUN_TEST(test_rotate_vector_heading_west_points_left);
    RUN_TEST(test_rotate_vector_heading_south_points_down);
    RUN_TEST(test_rotate_vector_matches_toScreen_bearing_mapping);

    RUN_TEST(test_alert_within_both_thresholds);
    RUN_TEST(test_alert_boundary_values_are_inclusive);
    RUN_TEST(test_no_alert_when_distance_exceeds_threshold);
    RUN_TEST(test_no_alert_when_altitude_exceeds_threshold);
    RUN_TEST(test_no_alert_when_both_exceed_threshold);
    RUN_TEST(test_emergency_always_alerts_regardless_of_range);

    return UNITY_END();
}
