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

#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"dcomp.lib")
#pragma comment(lib,"dwrite.lib")


#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <windows.h>
#include <wincodec.h>
#include <regex>
#include "iracing.h"
#include "Config.h"
#include "OverlayCover.h"
#include "OverlayRelative.h"
#include "OverlayInputs.h"
#include "OverlayStandings.h"
#include "OverlayDebug.h"
#include "OverlayDDU.h"

using namespace Microsoft::WRL;
using namespace std;

enum class Hotkey
{
    UiEdit,
    Standings,
    DDU,
    TargetLapUp,
    TargetLapDown,
    Inputs,
    Relative,
    Cover
};

static void registerHotkeys()
{
    UnregisterHotKey( NULL, (int)Hotkey::UiEdit );
    UnregisterHotKey( NULL, (int)Hotkey::Standings );
    UnregisterHotKey( NULL, (int)Hotkey::DDU );
    UnregisterHotKey(NULL, (int)Hotkey::TargetLapUp);
    UnregisterHotKey(NULL, (int)Hotkey::TargetLapDown);
    UnregisterHotKey( NULL, (int)Hotkey::Inputs );
    UnregisterHotKey( NULL, (int)Hotkey::Relative );
    UnregisterHotKey( NULL, (int)Hotkey::Cover );

    UINT vk, mod;

    if( parseHotkey( g_cfg.getString("General","ui_edit_hotkey","alt-j"),&mod,&vk) )
        RegisterHotKey( NULL, (int)Hotkey::UiEdit, mod, vk );

    if( parseHotkey( g_cfg.getString("OverlayStandings","toggle_hotkey","ctrl-space"),&mod,&vk) )
        RegisterHotKey( NULL, (int)Hotkey::Standings, mod, vk );

    if( parseHotkey( g_cfg.getString("OverlayDDU","toggle_hotkey","ctrl-1"),&mod,&vk) )
        RegisterHotKey( NULL, (int)Hotkey::DDU, mod, vk );

    if (parseHotkey(g_cfg.getString("OverlayDDU", "target_lap_up", "ctrl-."), &mod, &vk))
        RegisterHotKey(NULL, (int)Hotkey::TargetLapUp, mod, vk);
    if (parseHotkey(g_cfg.getString("OverlayDDU", "target_lap_down", "ctrl-,"), &mod, &vk))
        RegisterHotKey(NULL, (int)Hotkey::TargetLapDown, mod, vk);

    if( parseHotkey( g_cfg.getString("OverlayInputs","toggle_hotkey","ctrl-2"),&mod,&vk) )
        RegisterHotKey( NULL, (int)Hotkey::Inputs, mod, vk );

    if( parseHotkey( g_cfg.getString("OverlayRelative","toggle_hotkey","ctrl-3"),&mod,&vk) )
        RegisterHotKey( NULL, (int)Hotkey::Relative, mod, vk );

    if( parseHotkey( g_cfg.getString("OverlayCover","toggle_hotkey","ctrl-4"),&mod,&vk) )
        RegisterHotKey( NULL, (int)Hotkey::Cover, mod, vk );
}

static void handleConfigChange( vector<Overlay*> overlays, ConnectionStatus status )
{
    registerHotkeys();

    ir_handleConfigChange();

    for( Overlay* o : overlays )
    {
        o->enable( g_cfg.getBool(o->getName(),"enabled",true) && (
            status == ConnectionStatus::DRIVING ||
            status == ConnectionStatus::CONNECTED && o->canEnableWhileNotDriving() ||
            status == ConnectionStatus::DISCONNECTED && o->canEnableWhileDisconnected()
            ));
        o->configChanged();
    }
}

static void giveFocusToIracing()
{
    HWND hwnd = FindWindow( "SimWinClass", NULL );
    if( hwnd )
        SetForegroundWindow( hwnd );
}

// Cargar una imagen .png utilizando WIC
void LoadPNGImage(const wchar_t* filePath, ComPtr<IWICImagingFactory>& wicFactory, ComPtr<IWICBitmapDecoder>& decoder, ComPtr<IWICBitmapFrameDecode>& frame, ComPtr<IWICFormatConverter>& formatConverter) {

    // Carga el archivo PNG utilizando el decodificador de mapas de bits WIC
    wicFactory->CreateDecoderFromFilename(filePath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());

    // Obtiene el primer fotograma del archivo PNG
    decoder->GetFrame(0, frame.GetAddressOf());

    // Convierte el formato del fotograma a 32 bpp ARGB
    wicFactory->CreateFormatConverter(formatConverter.GetAddressOf());
    formatConverter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);

    /*if (FAILED(hr))
    {
        // Error al crear el decodificador
        return hr;
    }*/

}

static void LoadCarIcons(map<string, IWICFormatConverter*>& mapa) {
    const wchar_t* directory = L"./carIcons";

    CoInitialize(nullptr);

    ComPtr<IWICImagingFactory> wicFactory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> formatConverter;

    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory.GetAddressOf()));

    if (filesystem::exists(directory) && filesystem::is_directory(directory)) {
        for (const auto& archivo : filesystem::directory_iterator(directory)) {
            if (filesystem::is_regular_file(archivo)) {
                std::string ruta = archivo.path().string();

                int length = MultiByteToWideChar(CP_UTF8, 0, ruta.c_str(), -1, NULL, 0);
                wchar_t* ruta_wchar = new wchar_t[length];
                MultiByteToWideChar(CP_UTF8, 0, ruta.c_str(), -1, ruta_wchar, length);

                LoadPNGImage(ruta_wchar, wicFactory, decoder, frame, formatConverter);
                string name = archivo.path().filename().string();
                std::regex pattern("\\.\\w+$");
                mapa[regex_replace(name, pattern, "")] = formatConverter.Get();
            }
        }
    }
    else {
        cout << "Cars icons doesnt found" << endl;
    }
}

int main()
{
    // Bump priority up so we get time from the sim
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    // Load the config and watch it for changes
    g_cfg.load();
    g_cfg.watchForChanges();

    map<string, IWICFormatConverter*> mapa;
    LoadCarIcons(mapa);

    // Register global hotkeys
    registerHotkeys();

    printf("\n====================================================================================\n");
    printf("Welcome to iRon! This app provides a few simple overlays for iRacing.\n\n");
    printf("NOTE: Most overlays are only active when iRacing is running and the car is on track.\n\n");
    printf("Current hotkeys:\n");
    printf("    Move and resize overlays:     %s\n", g_cfg.getString("General","ui_edit_hotkey","").c_str() );
    printf("    Toggle standings overlay:     %s\n", g_cfg.getString("OverlayStandings","toggle_hotkey","").c_str() );
    printf("    Toggle DDU overlay:           %s\n", g_cfg.getString("OverlayDDU","toggle_hotkey","").c_str() );
    printf("    DDU Target lap up             %s\n", g_cfg.getString("OverlayDDU", "target_lap_up", "").c_str());
    printf("    DDU Target lap down           %s\n", g_cfg.getString("OverlayDDU", "target_lap_down", "").c_str());
    printf("    Toggle inputs overlay:        %s\n", g_cfg.getString("OverlayInputs","toggle_hotkey","").c_str() );
    printf("    Toggle relative overlay:      %s\n", g_cfg.getString("OverlayRelative","toggle_hotkey","").c_str() );
    printf("    Toggle cover overlay:         %s\n", g_cfg.getString("OverlayCover","toggle_hotkey","").c_str() );
    printf("\niRon will generate a file called \'config.json\' in its current directory. This file\n"\
           "stores your settings. You can edit the file at any time, even while iRon is running,\n"\
           "to customize your overlays and hotkeys.\n\n");
    printf("To exit iRon, simply close this console window.\n\n");
    printf("For the latest version ***of this fork*** or to submit bug reports, go to:\n\n        https://github.com/diegoschneider/iRon\n\n");
    printf("Many thanks to lespalt (https://github.com/lespalt/iRon) and frmjar (https://github.com/frmjar/iRon) for their work!\n\n");
    printf("Happy Racing!\n");
    printf("====================================================================================\n\n");

    // Create overlays
    vector<Overlay*> overlays;
    overlays.push_back( new OverlayCover() );
    overlays.push_back( new OverlayRelative() );
    overlays.push_back( new OverlayInputs() );
    overlays.push_back( new OverlayStandings( mapa ) );
    overlays.push_back( new OverlayDDU() );
#ifdef _DEBUG
    overlays.push_back( new OverlayDebug() );
#endif

    ConnectionStatus  status   = ConnectionStatus::UNKNOWN;
    bool              uiEdit   = false;
    unsigned          frameCnt = 0;

    while( true )
    {
        ConnectionStatus prevStatus       = status;
        SessionType      prevSessionType  = ir_session.sessionType;

        // Refresh connection and session info
        status = ir_tick();
        if( status != prevStatus )
        {
            if( status == ConnectionStatus::DISCONNECTED )
                printf("Waiting for iRacing connection...\n");
            else
                printf("iRacing connected (%s)\n", ConnectionStatusStr[(int)status]);

            // Enable user-selected overlays, but only if we're driving
            handleConfigChange( overlays, status );
        }

        if( ir_session.sessionType != prevSessionType )
        {
            for( Overlay* o : overlays )
                o->sessionChanged();
        }

        dbg( "connection status: %s, session type: %s, session state: %d, pace mode: %d, on track: %d, flags: 0x%X", ConnectionStatusStr[(int)status], SessionTypeStr[(int)ir_session.sessionType], ir_SessionState.getInt(), ir_PaceMode.getInt(), (int)ir_IsOnTrackCar.getBool(), ir_SessionFlags.getInt() );

        // Update/render overlays
        {
            if( !g_cfg.getBool("General", "performance_mode_30hz", false) )
            {
                // Update everything every frame, roughly every 16ms (~60Hz)
                for( Overlay* o : overlays )
                    o->update();
            }
            else
            {
                // To save perf, update half of the (enabled) overlays on even frames and the other half on odd, for ~30Hz overall
                int cnt = 0;
                for( Overlay* o : overlays )
                {
                    if( o->isEnabled() )
                        cnt++;

                    if( (cnt & 1) == (frameCnt & 1) )
                        o->update();
                }
            }
        }

        // Watch for config change signal
        if( g_cfg.hasChanged() )
        {
            g_cfg.load();
            handleConfigChange( overlays, status );
        }

        // Message pump
        MSG msg = {};
        while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            // Handle hotkeys
            if( msg.message == WM_HOTKEY )
            {
                if( msg.wParam == (int)Hotkey::UiEdit )
                {
                    uiEdit = !uiEdit;
                    for( Overlay* o : overlays )
                        o->enableUiEdit( uiEdit );

                    // When we're exiting edit mode, attempt to make iRacing the foreground window again for best perf
                    // without the user having to manually click into iRacing.
                    if( !uiEdit )
                        giveFocusToIracing();
                }
                else
                {
                    switch( msg.wParam )
                    {
                    case (int)Hotkey::Standings:
                        g_cfg.setBool( "OverlayStandings", "enabled", !g_cfg.getBool("OverlayStandings","enabled",true) );
                        break;
                    case (int)Hotkey::DDU:
                        g_cfg.setBool( "OverlayDDU", "enabled", !g_cfg.getBool("OverlayDDU","enabled",true) );
                        break;
                    case (int)Hotkey::Inputs:
                        g_cfg.setBool( "OverlayInputs", "enabled", !g_cfg.getBool("OverlayInputs","enabled",true) );
                        break;
                    case (int)Hotkey::Relative:
                        g_cfg.setBool( "OverlayRelative", "enabled", !g_cfg.getBool("OverlayRelative","enabled",true) );
                        break;
                    case (int)Hotkey::Cover:
                        g_cfg.setBool( "OverlayCover", "enabled", !g_cfg.getBool("OverlayCover","enabled",true) );
                        break;

                    case (int)Hotkey::TargetLapUp:
                        g_cfg.setInt("OverlayDDU", "fuel_target_lap", g_cfg.getInt("OverlayDDU", "fuel_target_lap", 0) + 1);
                        break;
                    case (int)Hotkey::TargetLapDown:
                        g_cfg.setInt("OverlayDDU", "fuel_target_lap", std::max( g_cfg.getInt("OverlayDDU", "fuel_target_lap", 0) - 1, 0) );
                        break;
                    }
                    
                    g_cfg.save();
                    handleConfigChange( overlays, status );
                }
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);            
        }

        frameCnt++;
    }

    for( Overlay* o : overlays )
        delete o;

    // Libera los recursos de COM
    CoUninitialize();
}
