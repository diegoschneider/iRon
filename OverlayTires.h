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

//#include <vector>
#include <algorithm>
#include <string>
#include "Overlay.h"
#include "Config.h"
#include "TelemetryHandler.h"
//#include "OverlayDebug.h"

class OverlayTires : public Overlay
{
    public:

        const float DefaultFontSize = 10.0f;

        OverlayTires(Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice)
            : Overlay("OverlayTires", d3dDevice)
        {}

    protected:

        enum class Columns { LL, LM, LR, RL, RM, RR };

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

            const std::string font = g_cfg.getString( m_name, "font", "Microsoft YaHei UI" );
            m_fontSize = g_cfg.getFloat( m_name, "font_size", DefaultFontSize );
            const int fontWeight = g_cfg.getInt( m_name, "font_weight", 500 );
            HRCHECK(m_dwriteFactory->CreateTextFormat( toWide(font).c_str(), NULL, (DWRITE_FONT_WEIGHT)fontWeight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, m_fontSize, L"en-us", &m_textFormat ));
            m_textFormat->SetParagraphAlignment( DWRITE_PARAGRAPH_ALIGNMENT_CENTER );
            m_textFormat->SetWordWrapping( DWRITE_WORD_WRAPPING_NO_WRAP );
            m_textFormat->SetTextAlignment( DWRITE_TEXT_ALIGNMENT_CENTER );

            HRCHECK(m_dwriteFactory->CreateTextFormat( toWide(font).c_str(), NULL, (DWRITE_FONT_WEIGHT)fontWeight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, m_fontSize*0.8f, L"en-us", &m_textFormatSmall ));
            m_textFormatSmall->SetParagraphAlignment( DWRITE_PARAGRAPH_ALIGNMENT_CENTER );
            m_textFormatSmall->SetWordWrapping( DWRITE_WORD_WRAPPING_NO_WRAP );

            // Determine widths of text columns
            m_columns.reset();
            m_columnWidth = computeTextExtent(L"999.9", m_dwriteFactory.Get(), m_textFormat.Get()).x;
            m_columns.add( (int)Columns::LL, m_columnWidth , m_fontSize/2 );
            m_columns.add( (int)Columns::LM, m_columnWidth , m_fontSize/2 );
            m_columns.add( (int)Columns::LR, m_columnWidth , m_fontSize/2, m_fontSize );
            m_columns.add( (int)Columns::RL, m_columnWidth , m_fontSize/2 );
            m_columns.add( (int)Columns::RM, m_columnWidth , m_fontSize/2 );
            m_columns.add( (int)Columns::RR, m_columnWidth , m_fontSize/2 );

            const int selfCarId = ir_session->cars[ir_session->driverCarIdx].carID;
            
            std::string mod_name = std::format("{}_{}", m_name, selfCarId);
            m_lowTemp = g_cfg.getFloat(mod_name, "TempLow", 45);
            m_goodTemp = g_cfg.getFloat(mod_name, "TempGood", 60);
            m_highTemp = g_cfg.getFloat(mod_name, "TempHigh", 90);
        }

        virtual void onUpdate()
        {

            if (ir_session->sessionType == SessionType::QUALIFY || ir_session->sessionType == SessionType::RACE) {

                wchar_t s[64] = L"Unavailable outside of practice";
                float w = computeTextExtent(s, m_dwriteFactory.Get(), m_textFormat.Get()).x;
                m_renderTarget->BeginDraw();
                m_brush->SetColor( float4(1.0, 1.0, 1.0f, 1.0f ));
                m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), 15, 15+w, 15, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER);
                m_renderTarget->EndDraw();
                return;
            }
            
            // Process last telemetry data
            TyreData* telemetry_data = g_telemetryHandler.getNextTyreData();

            m_columns.layout( (float)m_width - 20 );
            m_renderTarget->BeginDraw();
            
            wchar_t s[512];
            float4 col = float4(1.0f, 1.0f, 1.0f, 1.0f);
            const ColumnLayout::Column* clm = nullptr;
            D2D1_RECT_F r = {};
            D2D1_ROUNDED_RECT rr = {};
            
            const float xoff = m_fontSize;
            const float max_height = (m_height) / 2;
            const float w = m_columnWidth;
            float rectH = max_height - m_fontSize*3;
            float rectX, rectY, tempY, wearY;

            const int selfCarId = ir_session->cars[ir_session->driverCarIdx].carID;
            swprintf( s, _countof(s), L"CarID: %i", selfCarId); // carID
            m_brush->SetColor( col );
            m_text.render( m_renderTarget.Get(), s, m_textFormatSmall.Get(), 5, 100, 10, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );

		    for (int axle = 0; axle < 2; axle++) {
                wearY = std::max(xoff, ((xoff/2) + max_height) * axle);
                rectY = wearY + m_fontSize*0.5f;
                tempY = rectY+rectH + m_fontSize*0.5f;

			    for (int sect = 0; sect < 6; sect++) {
                    clm = m_columns.get(sect);

                    rectX = clm->textL;
                    r = { rectX, rectY, rectX+w, rectY+rectH };
                    m_brush->SetColor( getTireColor(telemetry_data->temp[axle][sect]) ); // temp
                    m_renderTarget->FillRectangle( &r, m_brush.Get() );

                    m_brush->SetColor( col );
                    swprintf( s, _countof(s), L"%.1f", telemetry_data->temp[axle][sect]); // temp
                    m_text.render( m_renderTarget.Get(), s, m_textFormat.Get(), clm->textL, clm->textR, tempY, m_brush.Get(), DWRITE_TEXT_ALIGNMENT_CENTER );

				}
		    }

            m_renderTarget->EndDraw();
        }

        float4 getTireColor(const float tyreData) {

            float red = std::min(1.0f, std::max(0.0f, ( (tyreData-m_goodTemp) / (m_highTemp-m_goodTemp) )));
            float blue = std::min(1.0f, std::max(0.0f, ( (m_goodTemp-tyreData) / (m_goodTemp-m_lowTemp) )));
            float green = 1.0f - (red + blue);

            return float4(red, green, blue, 1.0f);
        }

        virtual bool canEnableWhileNotDriving() const
        {
            return g_cfg.getBool(m_name, "enabled_while_not_driving", false);
        }

    protected:

        Microsoft::WRL::ComPtr<IDWriteTextFormat>  m_textFormat;
        Microsoft::WRL::ComPtr<IDWriteTextFormat>  m_textFormatSmall;

        ColumnLayout m_columns;
        TextCache    m_text;
        float        m_fontSize;
        float        m_columnWidth;
        float        m_lowTemp;
        float        m_goodTemp;
        float        m_highTemp;
};
