
#include <stdio.h>
#include <mutex>
#include <optional>
#include "irsdk/irsdk_defines.h"
#include "irsdk/irsdk_diskclient.h"


static const int TH_MAX_BUFFERS = 60;

void do_update_telemetry(const std::string path);

void merge_telemetry();

bool init(const std::string path);

void process_telemetry();


static const char* const TelemetryHandlerStr[2][6] = {
	{
		"LFtempL","LFtempM","LFtempR", "RFtempL","RFtempM","RFtempR"
	},
	{
		"LRtempL","LRtempM","LRtempR", "RRtempL","RRtempM","RRtempR"
	}
};

struct TyreData {
	float temp[2][6];
};

class TelemetryReader
{
	public:
		bool				init(const std::string path);
		void				getNextTyreData(TyreData &td);
		void				skipExcessData();
		void				finish();
		
	private:
		bool				m_ready = false;
		int					m_isOnTrack_idx = 0; // bool
		irsdkDiskClient		m_idk;
		int					m_telemetry_data_idx[2][6]; // Front/Rear, Left LMR / Right LMR
		int					m_telemetry_buffer_idx = 0;
		int					m_telemetry_buffer_maxidx = 0;
		TyreData			m_telemetry_buffer[TH_MAX_BUFFERS];

};

class TelemetryWriter
{
	public:
		bool init(const std::string path);
		bool append(const std::string path);
		void finish();
		
	private:
		irsdkDiskWriter		m_idk;
		irsdkDiskClient		m_idk_client;
		bool				m_ready = false;

};

class TelemetryHandler
{
	public:
		TelemetryHandler();
		void				updateTelemetryFile(const std::string path);
		void				mergeTelemetry(const std::string path);
		void				switchTelemetryReader();
		TyreData*			getNextTyreData();
		void				finish();

	private:
		TyreData			m_tyre_data;
		std::string			m_oldPath;
		std::string			m_telemetryFinalPath;
		std::thread			m_openTelemetryThread;
		std::mutex			m_openTelemetryThread_mtx;

		// The swapping telemetry readers
		TelemetryReader		m_telemetry_reader[2];
		bool				m_telemetry_reader_cur = 0;

		TelemetryWriter		m_telemetry_writer;
};

extern TelemetryHandler g_telemetryHandler;
