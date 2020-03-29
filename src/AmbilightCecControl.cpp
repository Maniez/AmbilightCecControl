//============================================================================
// Name        : test.cpp
// Author      :
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <iostream>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <ctime>
#include <time.h>

#include <libcec/cec.h>
#include <libcec/cecloader.h>
#include <libcec/cectypes.h>
using namespace std;

using namespace CEC;

ICECCallbacks g_callbacks;
libcec_configuration g_config;
int g_cecLogLevel(-1);
int g_cecDefaultLogLevel(CEC_LOG_ALL);
ICECAdapter *g_parser;
bool g_bSingleCommand(false);
std::string g_strPort;

void CecLogMessage(void *cbParam, const cec_log_message *message) {
	if (cbParam) {
		printf("\n");
	}
	if ((message->level & g_cecLogLevel) == message->level) {
		std::string strLevel;
		switch (message->level) {
		case CEC_LOG_ERROR:
			strLevel = "ERROR:   ";
			break;
		case CEC_LOG_WARNING:
			strLevel = "WARNING: ";
			break;
		case CEC_LOG_NOTICE:
			strLevel = "NOTICE:  ";
			break;
		case CEC_LOG_TRAFFIC:
			strLevel = "TRAFFIC: ";
			break;
		case CEC_LOG_DEBUG:
			strLevel = "DEBUG:   ";
			break;
		default:
			break;
		}

		printf("%s[%16lld]\t%s\n", strLevel.c_str(), message->time, message->message);

	}
}

int main() {

	g_config.Clear();
	g_callbacks.Clear();
	snprintf(g_config.strDeviceName, 13, "CECTester");
	g_config.clientVersion = LIBCEC_VERSION_CURRENT;
	g_config.bActivateSource = 0;
	g_callbacks.logMessage = &CecLogMessage;
//	  g_callbacks.keyPress        = &CecKeyPress;
//	  g_callbacks.commandReceived = &CecCommand;
//	  g_callbacks.alert           = &CecAlert;
	g_config.callbacks = &g_callbacks;

	if (g_cecLogLevel == -1)
		g_cecLogLevel = g_cecDefaultLogLevel;

	if (g_config.deviceTypes.IsEmpty()) {
		if (!g_bSingleCommand)
			std::cout << "No device type given. Using 'TV device'" << std::endl;
		g_config.deviceTypes.Add(CEC_DEVICE_TYPE_RECORDING_DEVICE);
	}

	g_parser = LibCecInitialise(&g_config);
	if (!g_parser) {
		std::cout << "Cannot load libcec.so" << std::endl;
		if (g_parser)
			UnloadLibCec(g_parser);
		return 1;
	}

	// init video on targets that need this
	g_parser->InitVideoStandalone();

	if (!g_bSingleCommand) {
		std::cout << std::endl << "CEC Parser created - libCEC version " << g_parser->VersionToString(g_config.serverVersion).c_str() << std::endl;
	}

	if (g_strPort.empty()) {
		if (!g_bSingleCommand)
			std::cout << "no serial port given. trying autodetect: ";
		cec_adapter_descriptor devices[10];
		uint8_t iDevicesFound = g_parser->DetectAdapters(devices, 10, NULL, true);
		if (iDevicesFound <= 0) {
			if (g_bSingleCommand)
				std::cout << "autodetect ";
			std::cout << "FAILED" << std::endl;
			UnloadLibCec(g_parser);
			return 1;
		} else {
			if (!g_bSingleCommand) {
				std::cout << std::endl << " path:     " << devices[0].strComPath << std::endl << " com port: " << devices[0].strComName << std::endl << std::endl;
			}
			g_strPort = devices[0].strComName;
		}
	}

	std::cout << "opening a connection to the CEC adapter..." << std::endl;

	if (!g_parser->Open(g_strPort.c_str())) {
		std::cout << std::endl << "unable to open the device on port %s" << g_strPort.c_str() << std::endl;
		UnloadLibCec(g_parser);
		return 1;
	}

	if (!g_bSingleCommand)
		std::cout << std::endl << "waiting for input" << std::endl;

	bool loopEnable = true;
	int isOn = -1;
	while (loopEnable) {
		// Check Time
		auto currentTime = std::chrono::system_clock::now();
		std::time_t time = std::chrono::system_clock::to_time_t(currentTime);
		struct tm *tmp = gmtime(&time);

		if (tmp->tm_isdst == 0) {
			tmp->tm_hour++;
		}

		bool timeEnableHyperion = false;
		if ((tmp->tm_hour >= 18) || (tmp->tm_hour < 5)) {
			timeEnableHyperion = true;
		}

		// Get CEC Status
		cec_power_status iPower = g_parser->GetDevicePowerStatus((cec_logical_address) 0);
		std::cout << std::endl << "power status: " << g_parser->ToString(iPower) << std::endl;

		// Decide to power of or on hyperion
		if ((iPower == cec_power_status::CEC_POWER_STATUS_ON || iPower == cec_power_status::CEC_POWER_STATUS_IN_TRANSITION_STANDBY_TO_ON) && (isOn != 1) && timeEnableHyperion) {
			system("hyperion-remote --clearall");
			system("hyperion-remote --luminanceMin 0.15");
			isOn = 1;
		} else if ((iPower == cec_power_status::CEC_POWER_STATUS_STANDBY || iPower == cec_power_status::CEC_POWER_STATUS_IN_TRANSITION_ON_TO_STANDBY
				|| iPower == cec_power_status::CEC_POWER_STATUS_UNKNOWN) && (isOn != 0)) {
			system("hyperion-remote --priority 0 --color black");
			system("hyperion-remote --luminanceMin 0.0");
			isOn = 0;
		}

		// Avoid excessive load on CEC connection
		sleep(5);
	}

	g_parser->Close();
	UnloadLibCec(g_parser);

	return 0;
}
