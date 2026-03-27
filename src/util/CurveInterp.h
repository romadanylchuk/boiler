#pragma once
#include "../model/AppState.h"

// Linear interpolation over CurvePoint array.
// Points must be sorted by outsideTemp ascending before calling.

class CurveInterp {
public:
    // Returns interpolated flow setpoint for given outside temperature.
    // Clamps to nearest point if outside is beyond curve range.
    // Returns defaultVal if curveCount < 2.
    static float interpolateFlow(const CurvePoint* points, uint8_t count,
                                 float outsideTemp, float defaultVal);

    // Same for return setpoint.
    static float interpolateReturn(const CurvePoint* points, uint8_t count,
                                   float outsideTemp, float defaultVal);

    // Sort points by outsideTemp (call after editing curve in config)
    static void sortPoints(CurvePoint* points, uint8_t count);

private:
    static float interpolate(float x,
                             float x0, float y0,
                             float x1, float y1);
};
