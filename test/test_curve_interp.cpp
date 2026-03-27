#include <unity.h>
#include "../src/util/CurveInterp.h"
#include "../src/AppState.h"

void test_interpolation_midpoint() {
    CurvePoint pts[] = {
        { -20, 65, 55 },
        {   0, 55, 45 },
        {  15, 30, 25 },
    };
    float flow = CurveInterp::interpolateFlow(pts, 3, -10, 55);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 60.0f, flow);
}

void test_clamp_below_min() {
    CurvePoint pts[] = { { -20, 65, 55 }, { 15, 30, 25 } };
    float flow = CurveInterp::interpolateFlow(pts, 2, -30, 55);
    TEST_ASSERT_EQUAL_FLOAT(65.0f, flow);
}

void test_clamp_above_max() {
    CurvePoint pts[] = { { -20, 65, 55 }, { 15, 30, 25 } };
    float flow = CurveInterp::interpolateFlow(pts, 2, 25, 55);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, flow);
}

void test_default_when_too_few_points() {
    CurvePoint pts[] = { { 0, 55, 45 } };
    float flow = CurveInterp::interpolateFlow(pts, 1, 0, 55);
    TEST_ASSERT_EQUAL_FLOAT(55.0f, flow);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_interpolation_midpoint);
    RUN_TEST(test_clamp_below_min);
    RUN_TEST(test_clamp_above_max);
    RUN_TEST(test_default_when_too_few_points);
    return UNITY_END();
}
