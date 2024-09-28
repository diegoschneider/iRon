/*
MIT License

Copyright (c) 2021-2022 L. E. Spalt

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
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

#include <assert.h>
#include <set>
#include "Overlay.h"
#include "Config.h"
#include "OverlayDebug.h"

using namespace std;

class OverlayStandings : public Overlay
{
public:

    const float DefaultFontSize = 15;
    const int defaultNumTopDrivers = 3;
    const int defaultNumAheadDrivers = 5;
    const int defaultNumBehindDrivers = 5;

    enum class Columns { POSITION, CAR_NUMBER, NAME, GAP, BEST, LAST, LICENSE, IRATING, CAR_BRAND, PIT, DELTA, L5, POSITIONS_GAINED };

    OverlayStandings(map<string, IWICFormatConverter*> mapa)
        : Overlay("OverlayStandings")
    {
        avgL5Times.reserve(IR_MAX_CARS);

        for (int i = 0; i < IR_MAX_CARS; ++i) {
            avgL5Times.emplace_back();
            avgL5Times[i].reserve(5);

            for (int j = 0; j < 5; ++j) {
                avgL5Times[i].push_back(0.0);
            }
        }

        this->mapa = mapa;
    }

protected:

    virtual void onEnable()
    {
        onConfigChanged();  // trigger font load
    }

    virtual void onDisable()
    {
        m_text.reset();
    }

    virtual void onConfigChanged()
    {
        m_text.reset( m_dwriteFactory.Get() );

        const string font = g_cfg.getString( m_name, "font", "Microsoft YaHei UI" );
        const float fontSize = g_cfg.getFloat( m_name, "font_size", DefaultFontSize );
        const int fontWeight = g_cfg.getInt( m_name, "font_weight", 500 );
        HRCHECK(m_dwriteFactory->CreateTextFormat( toWide(font).c_str(), NULL, (DWRITE_FONT_WEIGHT)fontWeight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"en-us", &m_textFormat ));
        m_textFormat->SetParagraphAlignment( DWRITE_PARAGRAPH_ALIGNMENT_CENTER );
        m_textFormat->SetWordWrapping( DWRITE_WORD_WRAPPING_NO_WRAP );

        HRCHECK(m_dwriteFactory->CreateTextFormat( toWide(font).c_str(), NULL, (DWRITE_FONT_WEIGHT)fontWeight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize*0.8f, L"en-us", &m_textFormatSmall ));
        m_textFormatSmall->SetParagraphAlignment( DWRITE_PARAGRAPH_ALIGNMENT_CENTER );
        m_textFormatSmall->SetWordWrapping( DWRITE_WORD_WRAPPING_NO_WRAP );

        // Determine widths of text columns
        m_columns.reset();
        m_columns.add( (int)Columns::POSITION,   computeTextExtent( L"P99", m_dwriteFactory.Get(), m_textFormat.Get() ).x, fontSize/2 );
        m_columns.add( (int)Columns::CAR_NUMBER, computeTextExtent( L"#999", m_dwriteFactory.Get(), m_textFormat.Get() ).x, fontSize/2 );
        m_columns.add( (int)Columns::NAME,       0, fontSize/2 );
        m_columns.add( (int)Columns::PIT,        computeTextExtent( L"P.Age", m_dwriteFactory.Get(), m_textFormat.Get() ).x, fontSize/2 );
        m_columns.add( (int)Columns::LICENSE,    computeTextExtent( L"A 4.44", m_dwriteFactory.Get(), m_textFormatSmall.Get() ).x, fontSize/6 );
        m_columns.add( (int)Columns::IRATING,    computeTextExtent( L" 9.9k ", m_dwriteFactory.Get(), m_textFormatSmall.Get() ).x, fontSize/6 );

        if (g_cfg.getBool(m_name, "show_car_brand", true))
            m_columns.add( (int)Columns::CAR_BRAND,  30, fontSize / 2);

        m_columns.add( (int)Columns::POSITIONS_GAINED, computeTextExtent(L"↑6", m_dwriteFactory.Get(), m_textFormat.Get()).x, fontSize / 2);
        m_columns.add( (int)Columns::GAP,        computeTextExtent(L"999.9", m_dwriteFactory.Get(), m_textFormat.Get()).x, fontSize / 2 );
        m_columns.add( (int)Columns::BEST,       computeTextExtent( L"99:99.999", m_dwriteFactory.Get(), m_textFormat.Get() ).x, fontSize/2 );

        if (g_cfg.getBool(m_name, "show_lap_time", true))
            m_columns.add( (int)Columns::LAST,   computeTextExtent( L"99:99.999", m_dwriteFactory.Get(), m_textFormat.Get() ).x, fontSize/2 );

        if (g_cfg.getBool(m_name, "show_delta", true))
            m_columns.add( (int)Columns::DELTA,  computeTextExtent( L"99.99", m_dwriteFactory.Get(), m_textFormat.Get() ).x, fontSize/2 );

        if (g_cfg.getBool(m_name, "show_L5", false))
            m_columns.add( (int)Columns::L5,     computeTextExtent(L"99.99.999", m_dwriteFactory.Get(), m_textFormat.Get()).x, fontSize / 2 );
    }

    virtual void onUpdate()
    {
        struct CarInfo {
            int     carIdx = 0;
            int     classIdx = 0;
            int     lapCount = 0;
            float   pctAroundLap = 0;
            int     lapGap = 0;
            float   gap = 0;
            float   delta = 0;
            int     position = 0;
            float   best = 0;
            float   last = 0;
            float   l5 = 0;
            bool    hasFastestLap = false;
            int     pitAge = 0;
            int     positionsChanged = 0;
        };

        struct classBestLap {
            int     carIdx = -1;
            float   best = FLT_MAX;
        };

        vector<CarInfo> carInfo;
        carInfo.reserve( IR_MAX_CARS );

        // Init array
        map<int, classBestLap> bestLapClass;
        int selfPosition = ir_getPosition(ir_session.driverCarIdx);
        boolean hasPacecar = false;

        for( int i=0; i<IR_MAX_CARS; ++i )
        {
            const Car& car = ir_session.cars[i];

            if (car.isPaceCar || car.isSpectator || car.userName.empty()) {
                hasPacecar = true;
                continue;
            }

            CarInfo ci;
            ci.carIdx       = i;
            ci.lapCount     = max( ir_CarIdxLap.getInt(i), ir_CarIdxLapCompleted.getInt(i) );
            ci.position     = ir_getPosition(i);
            ci.pctAroundLap = ir_CarIdxLapDistPct.getFloat(i);
            ci.gap          = ir_session.sessionType!=SessionType::RACE ? 0 : -ir_CarIdxF2Time.getFloat(i);
            ci.last         = ir_CarIdxLastLapTime.getFloat(i);
            ci.pitAge       = ir_CarIdxLap.getInt(i) - car.lastLapInPits;
            ci.positionsChanged = ir_getPositionsChanged(i);
            ci.classIdx     = ir_getClassId(ci.carIdx);

            ci.best         = ir_CarIdxBestLapTime.getFloat(i);
            if (ir_session.sessionType == SessionType::RACE && ir_SessionState.getInt() <= irsdk_StateWarmup || ir_session.sessionType == SessionType::QUALIFY && ci.best <= 0) {
                ci.best = car.qualy.fastestTime;
                for (int i = 0; i < IR_MAX_CARS; ++i) {
                    for (int j = 0; j < 5; ++j) {
                        avgL5Times[i][j] = 0.0;
                    }
                }
            }
                
            if (ir_CarIdxTrackSurface.getInt(ci.carIdx) == irsdk_NotInWorld) {
                switch (ir_session.sessionType) {
                    case SessionType::QUALIFY:
                        ci.best = car.qualy.fastestTime;
                        ci.last = car.qualy.lastTime;
                        break;
                    case SessionType::PRACTICE:
                        ci.best = car.practice.fastestTime;
                        ci.last = car.practice.lastTime;
                        break;
                    case SessionType::RACE:
                        ci.best = car.race.fastestTime;
                        ci.last = car.race.lastTime;
                        break;
                    default:
                        break;
                }               
            }

            if (!bestLapClass.contains(ci.classIdx)) {
                classBestLap classBest;
                bestLapClass.insert_or_assign(ci.classIdx, classBest);
            }

            if( ci.best > 0 && ci.best < bestLapClass[ci.classIdx].best) {
                bestLapClass[ci.classIdx].best = ci.best;
                bestLapClass[ci.classIdx].carIdx = hasPacecar ? ci.carIdx - 1 : ci.carIdx;               
            }
            
            if(ci.lapCount > 0)
                avgL5Times[ci.carIdx][ci.lapCount % 5] = ci.last;

            float total = 0;
            int conteo = 0;
            for (float time : avgL5Times[ci.carIdx]) {
                if (time > 0.0) {
                    total += time;
                    conteo++;
                }
            }

            ci.l5 = conteo ? total / conteo : 0.0F;

            carInfo.push_back(ci);
        }

        for (const auto& pair : bestLapClass)
        {
            if (pair.second.best > 0 && pair.second.carIdx >= 0)
                carInfo[pair.second.carIdx].hasFastestLap = true;
                string str = formatLaptime(pair.second.best);
        }

        const CarInfo ciSelf = carInfo[ir_PlayerCarIdx.getInt() > 0 ? hasPacecar ? ir_PlayerCarIdx.getInt() - 1 : ir_PlayerCarIdx.getInt() : 0];
        
        // Sort by position
        sort( carInfo.begin(), carInfo.end(),
            []( const CarInfo& a, const CarInfo& b ) {
                const int ap = a.position<=0 ? INT_MAX : a.position;
                const int bp = b.position<=0 ? INT_MAX : b.position;
                return ap < bp;
            } );

        // Compute lap gap to leader and compute delta
        int classLeader = -1;
        int carsInClass = 0;
        for( int i=0; i<(int)carInfo.size(); ++i )
        {
            CarInfo&       ci       = carInfo[i];
            if (ci.classIdx != ciSelf.classIdx)
                continue;

            carsInClass++;

            if (ci.position == 1) {
                classLeader = ci.carIdx;
            }

            ci.lapGap = ir_getLapDeltaToLeader( ci.carIdx, classLeader);
            ci.delta = ir_getDeltaTime( ci.carIdx, ir_session.driverCarIdx );

            if (ir_session.sessionType != SessionType::RACE) {
                if(classLeader != -1) {
                    ci.gap = ir_CarIdxF2Time.getFloat(ci.carIdx) - ir_CarIdxF2Time.getFloat(classLeader);
                    ci.gap = ci.gap < 0 ? 0 : ci.gap;
                }
                else {
                    ci.gap = 0;
                }
            }
        }

        const float  fontSize           = g_cfg.getFloat( m_name, "font_size", DefaultFontSize );
        const float  lineSpacing        = g_cfg.getFloat( m_name, "line_spacing", 8 );
        const float  lineHeight         = fontSize + lineSpacing;
        const float4 selfCol            = g_cfg.getFloat4( m_name, "self_col", float4(0.94f,0.67f,0.13f,1) );
        const float4 buddyCol           = g_cfg.getFloat4( m_name, "buddy_col", float4(0.2f,0.75f,0,1) );
        const float4 flaggedCol         = g_cfg.getFloat4( m_name, "flagged_col", float4(0.68f,0.42f,0.2f,1) );
        const float4 otherCarCol        = g_cfg.getFloat4( m_name, "other_car_col", float4(1,1,1,0.9f) );
        const float4 headerCol          = g_cfg.getFloat4( m_name, "header_col", float4(0.7f,0.7f,0.7f,0.9f) );
        const float4 carNumberTextCol   = g_cfg.getFloat4( m_name, "car_number_text_col", float4(0,0,0,0.9f) );
        const float4 alternateLineBgCol = g_cfg.getFloat4( m_name, "alternate_line_background_col", float4(0.5f,0.5f,0.5f,0.1f) );
        const float4 iratingTextCol     = g_cfg.getFloat4( m_name, "irating_text_col", float4(0,0,0,0.9f) );
        const float4 iratingBgCol       = g_cfg.getFloat4( m_name, "irating_background_col", float4(1,1,1,0.85f) );
        const float4 licenseTextCol     = g_cfg.getFloat4( m_name, "license_text_col", float4(1,1,1,0.9f) );
        const float4 fastestLapCol      = g_cfg.getFloat4( m_name, "fastest_lap_col", float4(1,0,1,1) );
        const float4 pitCol             = g_cfg.getFloat4( m_name, "pit_col", float4(0.94f,0.8f,0.13f,1) );
        const float4 deltaPosCol        = g_cfg.getFloat4( m_name, "delta_positive_col", float4(0.0f, 1.0f, 0.0f, 1.0f));
        const float4 deltaNegCol        = g_cfg.getFloat4( m_name, "delta_negative_col", float4(1.0f, 0.0f, 0.0f, 1.0f));
        const float  licenseBgAlpha     = g_cfg.getFloat( m_name, "license_background_alpha", 0.8f );
        int  numTopDrivers        = g_cfg.getInt(m_name, "num_top_drivers", defaultNumTopDrivers);
        int  numAheadDrivers      = g_cfg.getInt(m_name, "num_ahead_drivers", defaultNumAheadDrivers);
        int  numBehindDrivers     = g_cfg.getInt(m_name, "num_behind_drivers", defaultNumBehindDrivers);
        const bool   imperial           = ir_DisplayUnits.getInt() == 0;

        const float xoff = 10.0f;
        const float yoff = 10;
        m_columns.layout( (float)m_width - 2*xoff );
        float y = yoff + lineHeight/2;
        const float ybottom = m_height - lineHeight * 1.5f;

        const ColumnLayout::Column* clm = nullptr;
        wchar_t s[512];
        string str;
        D2D1_RECT_F r = {};
        D2D1_ROUNDED_RECT rr = {};

        m_renderTarget->BeginDraw();
        m_brush->SetColor( headerCol );

        // Headers
        clm = m_columns.get( (int)Columns::POSITION );
        swprintf( s, _countof(s), L"Pos." );
        m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );

        clm = m_columns.get( (int)Columns::CAR_NUMBER );
        swprintf( s, _countof(s), L"No." );
        m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );

        clm = m_columns.get( (int)Columns::NAME );
        swprintf( s, _countof(s), L"Driver" );
        m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_LEADING );

        clm = m_columns.get( (int)Columns::PIT );
        swprintf( s, _countof(s), L"P.Age" );
        m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );

        clm = m_columns.get( (int)Columns::LICENSE );
        swprintf( s, _countof(s), L"SR" );
        m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );

        clm = m_columns.get( (int)Columns::IRATING );
        swprintf( s, _countof(s), L"IR" );
        m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );

        if (clm = m_columns.get((int)Columns::CAR_BRAND)) {
            swprintf(s, _countof(s), L"  ");
            m_text.render(m_renderTarget.Get(), s, m_textFormat.Get(), xoff + clm->textL, xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
        }

        clm = m_columns.get((int)Columns::POSITIONS_GAINED);
        swprintf(s, _countof(s), L" ");
        m_text.render(m_renderTarget.Get(), s, m_textFormat.Get(), xoff + clm->textL, xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER);

        clm = m_columns.get((int)Columns::GAP);
        swprintf(s, _countof(s), L"Gap");
        m_text.render(m_renderTarget.Get(), s, m_textFormat.Get(), xoff + clm->textL, xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);

        clm = m_columns.get( (int)Columns::BEST );
        swprintf( s, _countof(s), L"Best" );
        m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING );

        if (clm = m_columns.get( (int)Columns::LAST ) ) {
            swprintf(s, _countof(s), L"Last");
            m_text.render(m_renderTarget.Get(), s, m_textFormat.Get(), xoff + clm->textL, xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
        }

        if (clm = m_columns.get((int)Columns::DELTA)) {
            swprintf(s, _countof(s), L"Delta");
            m_text.render(m_renderTarget.Get(), s, m_textFormat.Get(), xoff + clm->textL, xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
        }

        if (clm = m_columns.get((int)Columns::L5)) {
            swprintf(s, _countof(s), L"Last 5 avg");
            m_text.render(m_renderTarget.Get(), s, m_textFormat.Get(), xoff + clm->textL, xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
        }

        // Content
        
        int carsToDraw = ((ybottom - 2 * yoff) / lineHeight) -1 ;
        if (carsToDraw >= carsInClass) {
            numTopDrivers = carsToDraw;
        }
        else {
            // cars to add ahead = total cars - position
            numAheadDrivers += max((ciSelf.position - carsInClass + numBehindDrivers), 0);
            numBehindDrivers -= min(max((ciSelf.position - carsInClass + numBehindDrivers), 0), 2);
            numTopDrivers += max(carsToDraw - (numTopDrivers+numAheadDrivers+numBehindDrivers+2), 0);
            numBehindDrivers += max(carsToDraw - (ciSelf.position + numBehindDrivers), 0);

        }
        //printf("Cars to draw : %d", carsToDraw);
        //std::cout << std::endl;
        int drawnCars = 0;
        int selfClassDrivers = 0;
        bool skippedCars = false;
        for( int i=0; i<(int)carInfo.size(); ++i )
        {
            y = 2*yoff + lineHeight/2 + (drawnCars+1)*lineHeight;
            
            if (ir_CarIdxClass.getInt(carInfo[i].carIdx) != ir_PlayerCarClass.getInt()) {
                continue;
            }

            selfClassDrivers++;

            if( y+lineHeight/2 > ybottom )
                break;

            // Focus on the driver
            if (selfPosition > 0 && selfClassDrivers > numTopDrivers) {                
                if (selfClassDrivers < selfPosition - numAheadDrivers) {
                    if (!skippedCars) {
                        skippedCars = true;
                        drawnCars++;
                    }
                    continue;
                }
                if (selfClassDrivers > selfPosition + numBehindDrivers) {
                    continue;
                }
            }

            drawnCars++;

            // Alternating line backgrounds
            if(selfClassDrivers & 1 && alternateLineBgCol.a > 0 )
            {
                D2D1_RECT_F r = { 0, y-lineHeight/2, (float)m_width,  y+lineHeight/2 };
                m_brush->SetColor( alternateLineBgCol );
                m_renderTarget->FillRectangle( &r, m_brush.Get() );
            }

            const CarInfo&  ci  = carInfo[i];
            const Car&      car = ir_session.cars[ci.carIdx];

            // Dim color if player is disconnected.
            // TODO: this isn't 100% accurate, I think, because a car might be "not in world" while the player
            // is still connected? I haven't been able to find a better way to do this, though.
            const bool isGone = !car.isSelf && ir_CarIdxTrackSurface.getInt(ci.carIdx) == irsdk_NotInWorld;
            float4 textCol = car.isSelf ? selfCol : (car.isBuddy ? buddyCol : (car.isFlagged?flaggedCol:otherCarCol));
            if( isGone )
                textCol.a *= 0.5f;

            // Position
            if( ci.position > 0 )
            {
                clm = m_columns.get( (int)Columns::POSITION );
                m_brush->SetColor( textCol );
                swprintf( s, _countof(s), L"P%d", ci.position );
                m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING );
            }

            // Car number
            {
                clm = m_columns.get( (int)Columns::CAR_NUMBER );
                swprintf( s, _countof(s), L"#%S", car.carNumberStr.c_str() );
                r = { xoff+clm->textL, y-lineHeight/2, xoff+clm->textR, y+lineHeight/2 };
                rr.rect = { r.left-2, r.top+1, r.right+2, r.bottom-1 };
                rr.radiusX = 3;
                rr.radiusY = 3;
                m_brush->SetColor( textCol );
                m_renderTarget->FillRoundedRectangle( &rr, m_brush.Get() );
                m_brush->SetColor( carNumberTextCol );
                m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );
            }

            // Name
            {
                clm = m_columns.get( (int)Columns::NAME );
                m_brush->SetColor( textCol );
                swprintf( s, _countof(s), L"%S", car.teamName.c_str() );
                m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_LEADING );
            }

            // Pit age
            if( !ir_isPreStart() && (ci.pitAge>=0||ir_CarIdxOnPitRoad.getBool(ci.carIdx)) )
            {
                clm = m_columns.get( (int)Columns::PIT );
                m_brush->SetColor( pitCol );
                swprintf( s, _countof(s), L"%d", ci.pitAge );
                r = { xoff+clm->textL, y-lineHeight/2+2, xoff+clm->textR, y+lineHeight/2-2 };
                if( ir_CarIdxOnPitRoad.getBool(ci.carIdx) ) {
                    swprintf( s, _countof(s), L"PIT" );
                    m_renderTarget->FillRectangle( &r, m_brush.Get() );
                    m_brush->SetColor( float4(0,0,0,1) );
                }
                else {
                    swprintf( s, _countof(s), L"%d", ci.pitAge );
                    m_renderTarget->DrawRectangle( &r, m_brush.Get() );
                }
                m_text.render( m_renderTarget.Get(), s, m_textFormatSmall.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );
            }

            // License/SR
            {
                clm = m_columns.get( (int)Columns::LICENSE );
                swprintf( s, _countof(s), L"%C %.1f", car.licenseChar, car.licenseSR );
                r = { xoff+clm->textL, y-lineHeight/2, xoff+clm->textR, y+lineHeight/2 };
                rr.rect = { r.left+1, r.top+1, r.right-1, r.bottom-1 };
                rr.radiusX = 3;
                rr.radiusY = 3;
                float4 c = car.licenseCol;
                c.a = licenseBgAlpha;
                m_brush->SetColor( c );
                m_renderTarget->FillRoundedRectangle( &rr, m_brush.Get() );
                m_brush->SetColor( licenseTextCol );
                m_text.render( m_renderTarget.Get(), s, m_textFormatSmall.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );
            }

            // Irating
            {
                clm = m_columns.get( (int)Columns::IRATING );
                swprintf( s, _countof(s), L"%.1fk", (float)car.irating/1000.0f );
                r = { xoff+clm->textL, y-lineHeight/2, xoff+clm->textR, y+lineHeight/2 };
                rr.rect = { r.left+1, r.top+1, r.right-1, r.bottom-1 };
                rr.radiusX = 3;
                rr.radiusY = 3;
                m_brush->SetColor( iratingBgCol );
                m_renderTarget->FillRoundedRectangle( &rr, m_brush.Get() );
                m_brush->SetColor( iratingTextCol );
                m_text.render( m_renderTarget.Get(), s, m_textFormatSmall.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );
            }

            // Car brand
            if (clm = m_columns.get((int)Columns::CAR_BRAND))
            {
                string carNameLowerCase = toLowerCase(car.carName);

                IWICFormatConverter* valor = findAndDrawCar(carNameLowerCase, mapa);
                
                //Default value if cant find car
                if (valor == nullptr) {
                    
                    if (notFoundBrands.find(car.carName) == notFoundBrands.end() ) {
                        std::cout << "Couldn't find car '" << car.carName << "' in the carIcons folder." << std::endl;
                        notFoundBrands.insert(car.carName);
                    }

                    valor = findAndDrawCar("00error", mapa);
                }

                if (valor != nullptr) {
                    ID2D1Bitmap* pBitmap = NULL;
                    m_renderTarget->CreateBitmapFromWicBitmap(valor, nullptr, &pBitmap);
                    D2D1_RECT_F r = { xoff + clm->textL, y - lineHeight / 2, xoff + clm->textR, y + lineHeight / 2 };
                    m_renderTarget->DrawBitmap(pBitmap, r);
                    pBitmap->Release();
                }
                else {
                    std::cout << "Error rendering car brand!" << std::endl;
                }
            
            }

            // Positions gained
            {
                clm = m_columns.get((int)Columns::POSITIONS_GAINED);

                if (ci.positionsChanged == 0) {
                    swprintf(s, _countof(s), L"-");
                    m_text.render(m_renderTarget.Get(), s, m_textFormat.Get(), xoff + clm->textL, xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
                }
                else {
                    if (ci.positionsChanged > 0) {
                        swprintf(s, _countof(s), L"▲");
                        m_brush->SetColor(deltaPosCol);
                    }
                    else {
                        swprintf(s, _countof(s), L"▼");
                        m_brush->SetColor(deltaNegCol);
                    }
                    m_text.render(m_renderTarget.Get(), s, m_textFormat.Get(), xoff + clm->textL, xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);

                    m_brush->SetColor(textCol);
                    swprintf(s, _countof(s), L"%d", abs(ci.positionsChanged));

                    m_text.render(m_renderTarget.Get(), s, m_textFormat.Get(), xoff + clm->textL, 17 + xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
                }
                
            }

            // Gap
            if (ci.lapGap || ci.gap)
            {
                clm = m_columns.get((int)Columns::GAP);
                if (ci.lapGap < 0)
                    swprintf(s, _countof(s), L"%d L", ci.lapGap);
                else
                    swprintf(s, _countof(s), L"%.01f", ci.gap);
                m_brush->SetColor(textCol);
                m_text.render(m_renderTarget.Get(), s, m_textFormat.Get(), xoff + clm->textL, xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
            }

            // Best
            {
                clm = m_columns.get( (int)Columns::BEST );
                str.clear();
                if( ci.best > 0 )
                    str = formatLaptime( ci.best );
                m_brush->SetColor( ci.hasFastestLap ? fastestLapCol : textCol);
                m_text.render( m_renderTarget.Get(), toWide(str).c_str(), m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING );
            }

            // Last
            if (clm = m_columns.get((int)Columns::LAST))
            {
                str.clear();
                if( ci.last > 0 )
                    str = formatLaptime( ci.last );
                m_brush->SetColor(textCol);
                m_text.render( m_renderTarget.Get(), toWide(str).c_str(), m_textFormat.Get(), xoff+clm->textL, xoff+clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING );
            }

            // Delta
            if (clm = m_columns.get((int)Columns::DELTA))
            {
                if (ci.delta)
                {
                    swprintf(s, _countof(s), L"%.01f", abs(ci.delta));
                    if (ci.delta > 0)
                        m_brush->SetColor(deltaPosCol);
                    else
                        m_brush->SetColor(deltaNegCol);
                    m_text.render(m_renderTarget.Get(), s, m_textFormat.Get(), xoff + clm->textL, xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
                }
            }

            // Average 5 laps
            if (clm = m_columns.get((int)Columns::L5))
            {
                str.clear();
                if (ci.l5 > 0 && selfPosition > 0) {
                    str = formatLaptime(ci.l5);
                    if (ci.l5 >= ciSelf.l5)
                        m_brush->SetColor(deltaPosCol);
                    else
                        m_brush->SetColor(deltaNegCol);
                }
                else
                    m_brush->SetColor(textCol);
                
                m_text.render(m_renderTarget.Get(), toWide(str).c_str(), m_textFormat.Get(), xoff + clm->textL, xoff + clm->textR, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
            }
        }
        
        // Footer
        {
            float trackTemp = ir_TrackTempCrew.getFloat();
            char  tempUnit  = 'C';

            if( imperial ) {
                trackTemp = celsiusToFahrenheit( trackTemp );
                tempUnit  = 'F';
            }

            int hours, mins, secs;

            ir_getSessionTimeRemaining(hours, mins, secs);
            const int laps = max(ir_CarIdxLap.getInt(ir_session.driverCarIdx), ir_CarIdxLapCompleted.getInt(ir_session.driverCarIdx));
            const int remainingLaps = ir_getLapsRemaining();
            const int irTotalLaps = ir_SessionLapsTotal.getInt();
            int totalLaps = remainingLaps;
            
            if (irTotalLaps == 32767)
                totalLaps = laps + remainingLaps;
            else
                totalLaps = irTotalLaps;

            m_brush->SetColor(float4(1,1,1,0.4f));
            m_renderTarget->DrawLine( float2(0,ybottom),float2((float)m_width,ybottom),m_brush.Get() );
            swprintf( s, _countof(s), L"SoF: %d       Track Temp: %.1f°%c      Session end: %d:%02d:%02d       Laps: %d/%s%d", ir_session.sof, trackTemp, tempUnit, hours, mins, secs, laps, (irTotalLaps == 32767 ? "~" : ""), totalLaps);
            y = m_height - (m_height-ybottom)/2;
            m_brush->SetColor( headerCol );
            m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), xoff, (float)m_width-2*xoff, y, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );
        }

        m_renderTarget->EndDraw();
    }

    virtual bool canEnableWhileNotDriving() const
    {
        return true;
    }

protected:

    Microsoft::WRL::ComPtr<IDWriteTextFormat>  m_textFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>  m_textFormatSmall;

    ColumnLayout m_columns;
    TextCache    m_text;
    vector<vector<float>> avgL5Times;
    map<string, IWICFormatConverter*> mapa;
    std::set<std::string> notFoundBrands;
};
