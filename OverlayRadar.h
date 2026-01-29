/*
MIT License

Copyright (c) 2021-2022 L. E. Spalt, Diego Schneider

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to w5m the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <array>
#include "Overlay.h"
#include "Config.h"
#include "OverlayDebug.h"

static struct CarInfo {
    int     carIdx = 0;
    float   deltaMts = 0;
    int     carID = 0;
};

class OverlayRadar : public Overlay
{
public:

    OverlayRadar(Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice)
        : Overlay("OverlayRadar", d3dDevice)
    {}

protected:

    virtual float2 getDefaultSize()
    {
        return float2(400, 400);
    }

    virtual void onConfigChanged()
    {

    }

    virtual void onUpdate()
    {
        const float w = (float)m_width;
        const float h = (float)m_height;

        const float cornerRadius = g_cfg.getFloat(m_name, "corner_radius", 2.0f);
        const float markerWidth = g_cfg.getFloat(m_name, "marker_width", 20.0f);

        std::vector<CarInfo> radarInfo;
        radarInfo.reserve(IR_MAX_CARS);
        const float selfLapDistPct = ir_LapDistPct.getFloat();
        const float trackLength = ir_LapDist.getFloat() / selfLapDistPct;
        const float maxDist = g_cfg.getFloat(m_name, "max_distance", 7.0f);

        // Populate RadarInfo
        for (int i = 0; i < IR_MAX_CARS; ++i)
        {
            const Car& car = g_ir_session->cars[i];
            const int lapcountCar = ir_CarIdxLap.getInt(i);

            // Ignore pace car and cars in pits
            if (lapcountCar >= 0 && !car.isSpectator && car.carNumber >= 0 && !car.isPaceCar && !ir_CarIdxOnPitRoad.getBool(i))
            {
                const float carLapDistPct = ir_CarIdxLapDistPct.getFloat(i);
                const bool wrap = fabsf(selfLapDistPct - carLapDistPct) > 0.5f;
                float lapDistPctDelta = selfLapDistPct - carLapDistPct;

                // Account for start/finish line
                if (wrap) {
                    if (selfLapDistPct > carLapDistPct) {
                        lapDistPctDelta -= 1;
                    }
                    else {
                        lapDistPctDelta += 1;
                    }
                }
                const float deltaMts = lapDistPctDelta * trackLength;

                // Filter the list, we dont care about far away cars
                // TODO: 5.0f? Shouldn't be by largest car length or something like that?
                if (fabs(deltaMts) < maxDist + 5.0f)
                    radarInfo.emplace_back(i, deltaMts, car.carID);

            }
        }

        std::sort(radarInfo.begin(), radarInfo.end(),
            [](const CarInfo& a, const CarInfo& b) {return a.deltaMts > b.deltaMts;});

        // Locate our driver's index in the new array, and nearest Ahead/Behind
        int selfRadarInfoIdx = -1, nearAhead = -1, nearBehind = -1;

        for (int i = 0; i < (int)radarInfo.size(); ++i)
        {
            const CarInfo ci = radarInfo[i];
            if (ci.carIdx == g_ir_session->driverCarIdx)
            {
                selfRadarInfoIdx = i;

                if (i > 0) {
                    nearBehind = i - 1;
                }
                if (i + 1 < (int)radarInfo.size()) {
                    nearAhead = i + 1;
                }
            }
        }

        float nearAheadDeltaMts = 0;
        if (nearAhead != -1 ) {
            nearAheadDeltaMts = radarInfo[nearAhead].deltaMts;
        }
        dbg("Nearahead: %d - %f ", nearAhead, nearAheadDeltaMts);

        // Something's wrong if we didn't find our driver. Bail.
        if (selfRadarInfoIdx < 0)
            return;

        // Update car lengths if just cleared
        check_update_car_lengths(radarInfo, selfRadarInfoIdx, nearAhead, nearBehind);

        std::string s = "CarLen: ";
        s.reserve(256);
        for (const auto carLenData : m_carLength) {
            char t[32];
            snprintf(t, sizeof(t), "%d: %f - ", carLenData.first, carLenData.second);
            s += t;
        }
        dbg(s.c_str());

        D2D1_ROUNDED_RECT lRect, rRect;
        int carsNear = 0;
        const int carLeftRight = ir_CarLeftRight.getInt();

        m_renderTarget->BeginDraw();
        for (const CarInfo ci : radarInfo) {

            const float carLength = max(m_carLength[ci.carID], 4.0f);

            if (fabsf(ci.deltaMts) > maxDist+carLength || ci.deltaMts == 0)
                continue;

            carsNear++;

            const float rect_top = calculate_radar_Y(ci.deltaMts, maxDist);
            const float rect_bot = calculate_radar_Y(ci.deltaMts+carLength, maxDist);
            dbg("Deltamts: %f", ci.deltaMts); 

            // Left side
            lRect.rect = { 0, h * rect_top, markerWidth, h * rect_bot };
            lRect.radiusX = cornerRadius;
            lRect.radiusY = cornerRadius;

            // Right side
            rRect.rect = { w - markerWidth, h * rect_top, w, h * rect_bot };
            rRect.radiusX = cornerRadius;
            rRect.radiusY = cornerRadius;

            m_brush->SetColor(g_cfg.getFloat4(m_name, "car_near_fill_col", float4(1.0f, 0.2f, 0.0f, 0.5f)));

            // Paint left
            switch (carLeftRight) {
                case irsdk_LRCarLeft:
                case irsdk_LR2CarsLeft:
                case irsdk_LRCarLeftRight:
                    m_brush->SetColor(g_cfg.getFloat4(m_name, "car_near_fill_col", float4(1.0f, 0.2f, 0.0f, 0.5f)));
                    break;
                default:
                    m_brush->SetColor(g_cfg.getFloat4(m_name, "car_far_fill_col", float4(1.0f, 1.0f, 0.0f, 0.5f)));
            }
            m_renderTarget->FillRoundedRectangle(&lRect, m_brush.Get());

            switch (carLeftRight) {
                case irsdk_LRCarRight:
                case irsdk_LR2CarsRight:
                case irsdk_LRCarLeftRight:
                    m_brush->SetColor(g_cfg.getFloat4(m_name, "car_near_fill_col", float4(1.0f, 0.2f, 0.0f, 0.5f)));
                    break;
                default:
                    m_brush->SetColor(g_cfg.getFloat4(m_name, "car_far_fill_col", float4(1.0f, 1.0f, 0.0f, 0.5f)));
            }
            m_renderTarget->FillRoundedRectangle(&rRect, m_brush.Get());
        }

        if (carsNear > 0 || true) {
            D2D1_RECT_F lRect, rRect;

            // Car limits Marks
            float carLimitsMarkLen = g_cfg.getFloat(m_name, "car_limits_mark_len", 10.0f);
            float rect_top = calculate_radar_Y(0+carLimitsMarkLen, maxDist);
            float rect_bot = calculate_radar_Y(0, maxDist);
            m_brush->SetColor(g_cfg.getFloat4(m_name, "car_limits_fill_col", float4(0.2f, 0.2f, 0.2f, 0.8f)));

            // Left side
            lRect = D2D1::RectF(0, h * rect_top, markerWidth, h * rect_bot);
            // Right side
            rRect = D2D1::RectF(w - markerWidth, h * rect_top, w, h * rect_bot);

            m_renderTarget->FillRectangle(&lRect, m_brush.Get());
            m_renderTarget->FillRectangle(&rRect, m_brush.Get());


            const float selfLen = m_carLength[radarInfo[selfRadarInfoIdx].carID];
            rect_top = calculate_radar_Y( selfLen, maxDist);
            rect_bot = calculate_radar_Y(selfLen+carLimitsMarkLen, maxDist);
            // Left side
            lRect = D2D1::RectF(0, h * rect_top, markerWidth, h * rect_bot);
            // Right side
            rRect = D2D1::RectF(w - markerWidth, h * rect_top, w, h * rect_bot);

            m_renderTarget->FillRectangle(&lRect, m_brush.Get());
            m_renderTarget->FillRectangle(&rRect, m_brush.Get());
        }

        m_renderTarget->EndDraw();
    }

    // Uhm... isn't the deque ordered now?
    float calculate_median_from_deque(const std::deque<float>& deque) {

        std::vector<float> tmpVec;
        tmpVec.reserve((int)deque.size());
        for (float v : deque) {
            tmpVec.push_back(v);
        }

        auto m = tmpVec.begin() + tmpVec.size() / 2;
        std::nth_element(tmpVec.begin(), m, tmpVec.end());
        return tmpVec[tmpVec.size() / 2];
    }

    // This uses -value as the coords are top to bottom!
    float calculate_radar_Y(float value, float maxDist) {
        const float carOffset = g_cfg.getFloat(m_name, "car_offset", 2.0f);
        const float clamp_max = maxDist - carOffset;
        const float clamp_min = -maxDist - carOffset;
        return min(max(0.0f + (1.0f / (clamp_max - clamp_min)) * (value - clamp_min), 0.0f), 1.0f);
        //output = output_start + ((output_end - output_start) / (input_end - input_start)) * (input - input_start)

        //return min(max(1.0f + (-1.0f / (2 * clamp)) * (-value + clamp), 0.0f), 1.0f);
    }

    void update_car_length(int carID, float deltaMts) {

        const int nearestClearQueueSize = g_cfg.getInt(m_name, "nearest_clear_queue_size", 5);
        if (deltaMts > 1.5f && deltaMts < 5.5f) {
            std::cout << "Updating! ";
            m_carLengthCalculationData[carID].push_back(deltaMts);
            std::sort(m_carLengthCalculationData[carID].begin(), m_carLengthCalculationData[carID].end(),
                [](const float a, const float b) {return a > b;});
            float carLen = m_carLength[carID];

            while (m_carLengthCalculationData[carID].size() > nearestClearQueueSize) {
                printf("Dropping: F:%f B:%f ", m_carLengthCalculationData[carID].front(), m_carLengthCalculationData[carID].back());
                m_carLengthCalculationData[carID].pop_front();
                m_carLengthCalculationData[carID].pop_back();
            }
            

            carLen = calculate_median_from_deque(m_carLengthCalculationData[carID]);
            m_carLength[carID] = carLen;
            printf(" new carLength: %f", carLen);
        }
    }

    void check_update_car_lengths(const std::vector<CarInfo> radarInfo, int selfIdx, int aheadIdx, int behindIdx) {
        
        const int selfCarID = radarInfo[selfIdx].carID;
        const int carLeftRight = ir_CarLeftRight.getInt();

        if (carLeftRight == irsdk_LRClear) {
            if (!m_areWeClear) {
                m_areWeClear = true;
                // Just got cleared. Calc!!

                // We got a car ahead
                if (aheadIdx != -1) {

                    std::cout << "Checking len ahead... ";

                    // ahead == negative numbers // skip too low values, maybe a spinout
                    const float deltaMts = -radarInfo[aheadIdx].deltaMts;
                    const int carID = radarInfo[aheadIdx].carID;
                    printf("%d: %f - ", carID, deltaMts);
                    update_car_length(carID, deltaMts);
                    std::cout << std::endl;
                }
                
                // We got a car behind, this updates OUR car!
                if (behindIdx != -1) {
                    std::cout << "Checking len behind...";
                    // behind == positive numbers // skip too low values, maybe a spinout
                    const float deltaMts = radarInfo[behindIdx].deltaMts;
                    printf("%d: %f - ", selfCarID,deltaMts);
                    update_car_length(selfCarID, deltaMts);
                    std::cout << std::endl;
                }
        
            }
        }
        else {
            // Should we track cars here, such that we can pre-empt if we are clearing them from ahead/behind?
            // That would be, at least, cool.
            // And would let us get a good first read on car lengths (if we don't hardcode the car lengths eventually)

            m_areWeClear = false;
        }

        
    }

protected:

    bool  m_areWeClear = true;
    std::map<int, std::deque<float> > m_carLengthCalculationData;
    std::map<int, float> m_carLength;
#pragma message ("WARNING: We never free the m_carLength and m_carLengthCalculationData std::map's")
};
