#include "CurveInterp.h"
#include <algorithm>

float CurveInterp::interpolate(float x, float x0, float y0, float x1, float y1) {
    if (x1 == x0) return y0;
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

void CurveInterp::sortPoints(CurvePoint* points, uint8_t count) {
    for (uint8_t i = 0; i < count - 1; i++) {
        for (uint8_t j = i + 1; j < count; j++) {
            if (points[j].outsideTemp < points[i].outsideTemp) {
                CurvePoint tmp = points[i];
                points[i] = points[j];
                points[j] = tmp;
            }
        }
    }
}

float CurveInterp::interpolateFlow(const CurvePoint* pts, uint8_t count,
                                   float outside, float defaultVal) {
    if (count < 2) return defaultVal;

    // Clamp below lowest point
    if (outside <= pts[0].outsideTemp) return pts[0].flowSetpoint;
    // Clamp above highest point
    if (outside >= pts[count - 1].outsideTemp) return pts[count - 1].flowSetpoint;

    // Find surrounding segment
    for (uint8_t i = 0; i < count - 1; i++) {
        if (outside >= pts[i].outsideTemp && outside <= pts[i + 1].outsideTemp) {
            return interpolate(outside,
                               pts[i].outsideTemp,   pts[i].flowSetpoint,
                               pts[i+1].outsideTemp, pts[i+1].flowSetpoint);
        }
    }
    return defaultVal;
}

float CurveInterp::interpolateReturn(const CurvePoint* pts, uint8_t count,
                                     float outside, float defaultVal) {
    if (count < 2) return defaultVal;

    if (outside <= pts[0].outsideTemp) return pts[0].returnSetpoint;
    if (outside >= pts[count - 1].outsideTemp) return pts[count - 1].returnSetpoint;

    for (uint8_t i = 0; i < count - 1; i++) {
        if (outside >= pts[i].outsideTemp && outside <= pts[i + 1].outsideTemp) {
            return interpolate(outside,
                               pts[i].outsideTemp,   pts[i].returnSetpoint,
                               pts[i+1].outsideTemp, pts[i+1].returnSetpoint);
        }
    }
    return defaultVal;
}
