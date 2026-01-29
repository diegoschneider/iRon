
#include <stdio.h>
#include <string>
#include <optional>
#include <thread>
// #include <iostream>
#include "irsdk/irsdk_defines.h"
#include "irsdk/irsdk_diskclient.h"
#include "TelemetryHandler.h"
#include "OverlayDebug.h"
#include "iracing.h"

TelemetryHandler g_telemetryHandler;

#define DEBUG_TELEMETRY

/******
* To get the tyre temperature, we need to ask the game to cut the telemetry onto a new file via the command "irsdk_TelemCommand_Restart"
* We do that on main.cpp, only when the tyre overlay is active, and iracing.cpp checks the new file path on updateSessionStringData
*
* The sessionString tells us which telemetry file is currently being written. When it changes to a new one,
* we take the old, load it, and show it. Cut, load, show. Cut, load, show.
* 
* Meanwhile, we should reassemble all those fragments without losing data :)
* 
******/


///////////////////////////////////////////
/////////////////////////////// Telemetry Reader
///////////////////////////////////////////

// In a new thread, open the next telemetry file in the free telemetryReader, and switch to this new reader
void openTelemetry(const std::string path, TelemetryReader *telemetryReader, std::mutex *openTelemetryMutex) {
	
	if (!openTelemetryMutex->try_lock()) 
	{
		printf("Mutex not unlocked! Still processing the previous telemetry?\n");
		return;
	}

	if (telemetryReader->init(path)) {
		g_telemetryHandler.switchTelemetryReader();
	}
	else {
		printf("Telemetry read failed! %s\n", path.c_str());
	}

	openTelemetryMutex->unlock();
}

// Init and load the tyre temperature data into the buffer
bool TelemetryReader::init(const std::string path)
{

	if (m_idk.isFileOpen()) m_idk.closeFile();

	bool done_open = m_idk.openFile(path.c_str());
	// Try again once
	if (!done_open)
	{
		printf("Telemetry read failed! (retrying) %s\n", path.c_str());
		Sleep(50);
		done_open = m_idk.openFile(path.c_str());
	}
	// Could not open
	if (!done_open) return false;


	m_isOnTrack_idx = m_idk.getVarIdx("IsOnTrack");

	if (m_isOnTrack_idx != -1) {

		//const int MAX_STR = 512;
		//char tstr[MAX_STR];

		//if (1 == m_idk.getSessionStrVal("DriverInfo:DriverCarIdx:", tstr, MAX_STR))
		//{
		// Get var index
		for (int axle = 0; axle < 2; axle++) {
			for (int sect = 0; sect < 6; sect++) {
				m_telemetry_data_idx[axle][sect] = m_idk.getVarIdx(TelemetryHandlerStr[axle][sect]);
			}
		}

		skipExcessData();

		// Read the last TH_MAX_BUFFERS values into the buffer
		int i;
		for (i = 0; i <= TH_MAX_BUFFERS; i++) {
			if (m_idk.getNextData()) {
				TyreData* td = &m_telemetry_buffer[i];
				if (m_idk.getVarBool(m_isOnTrack_idx)) {
					for (int axle = 0; axle < 2; axle++) {
						for (int sect = 0; sect < 6; sect++) {
							td->temp[axle][sect] = m_idk.getVarFloat(m_telemetry_data_idx[axle][sect]);
						}
					}
				}
			}
			else {
				break;
			}
		}
		m_telemetry_buffer_maxidx = i;
		m_idk.closeFile();
		m_ready = true;
#if defined(DEBUG_TELEMETRY)
		printf("Loaded %d buffers\n", m_telemetry_buffer_maxidx);
#endif
		return true;
	}

	return false;
}

void TelemetryReader::getNextTyreData(TyreData& td)
{
	if (!m_ready) return;

	// No more tyre data!
	if (m_telemetry_buffer_idx >= m_telemetry_buffer_maxidx) {
		finish();
		return;
	}

	// Copy the TyreData
	memcpy(&td, &m_telemetry_buffer[m_telemetry_buffer_idx], sizeof(TyreData));
	// Go to next index
	m_telemetry_buffer_idx++;
}

void TelemetryReader::skipExcessData() {
	int dataCount = m_idk.getDataCount();
#if defined(DEBUG_TELEMETRY)
	printf("Data count: %d - Skipping %d values\n", dataCount, dataCount - TH_MAX_BUFFERS);
#endif

	// Only leave the last TH_MAX_BUFFERS data points 
	if (!m_idk.skipData(std::max(0, dataCount - TH_MAX_BUFFERS))) {
		printf("Error skipping data!");
		finish();
	}
}

void TelemetryReader::finish() {
	m_ready = false;
	m_telemetry_buffer_idx = 0;
	m_idk.closeFile();
}



///////////////////////////////////////////
/////////////////////// Telemetry Writer
///////////////////////////////////////////

bool TelemetryWriter::init(const std::string path) {
	if (m_idk.isFileOpen()) m_idk.closeFile();

	bool done_open = m_idk.openFile(path.c_str());
	// Try again once
	if (!done_open)
	{
		printf("Telemetry writer open failed! (retrying) %s\n", path.c_str());
		Sleep(50);
		done_open = m_idk.openFile(path.c_str());
	}
	// Could not open
	if (!done_open) {
		printf("Telemetry writer failed, manual merging will be needed!!!");
		return false;
	}
	
	m_ready = true;
	return true;
}

bool TelemetryWriter::append(const std::string path) {
	if (!m_ready) return false;

	printf("TelemetryWriter::append : not implemented!");
	return true;
}

///////////////////////////////////////////
////////////////////// Telemetry Handler
///////////////////////////////////////////

TelemetryHandler::TelemetryHandler() {
	TyreData m_tyre_data;
}

// iRacing.cpp notified us!
void TelemetryHandler::updateTelemetryFile(const std::string path) {

	// When the first telemetry filename comes in, skip and save the filename.
	// When the second telemetry filename comes in, we process the first one.

	// FIXME: If you don't close the overlay between sessions, it will open the Last-session-Last-telemetry file for one second instead of starting at 0.0
	
	if (!m_oldPath.empty() && path != m_oldPath ) {
#if defined(DEBUG_TELEMETRY)
		printf("Opening %s\n", m_oldPath.c_str());
		printf("Merging old file into %s\n", m_telemetryFinalPath.c_str());
#endif
		m_openTelemetryThread = std::thread(openTelemetry, m_oldPath, &m_telemetry_reader[!m_telemetry_reader_cur], &m_openTelemetryThread_mtx); // !cur, dumbass
		m_openTelemetryThread.detach();
		
		mergeTelemetry(m_oldPath);
	}

	m_oldPath.assign(path);
	if (m_telemetryFinalPath.empty()) {
		m_telemetryFinalPath.assign(path);
#if defined(DEBUG_TELEMETRY)
		printf("Telemetry final path: %s\n", m_telemetryFinalPath.c_str());
		m_telemetry_writer.init(m_telemetryFinalPath);
#endif
	}
}

TyreData* TelemetryHandler::getNextTyreData() {

	// If no new value, will not update, so we return old data
	m_telemetry_reader[m_telemetry_reader_cur].getNextTyreData(m_tyre_data);
	
	return &m_tyre_data;

}

void TelemetryHandler::mergeTelemetry(const std::string path) {
	if (m_telemetryFinalPath.empty()) return; // No telemetry active

#if defined(DEBUG_TELEMETRY)
	printf("Merging telemetry at %s\n", m_telemetryFinalPath.c_str());
#endif

	m_telemetry_writer.append(path);
}

void TelemetryHandler::finish() {
	// Do not allow opening new telemetries meanwhile
	m_openTelemetryThread_mtx.lock();
	
	m_telemetry_reader[m_telemetry_reader_cur].finish();
	m_telemetryFinalPath.clear();

	m_openTelemetryThread_mtx.unlock();
}

void TelemetryHandler::switchTelemetryReader() {
	m_telemetry_reader[m_telemetry_reader_cur].finish();
	m_telemetry_reader_cur = !m_telemetry_reader_cur;
}
