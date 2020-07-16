//============================================================================
// Name        : test.cpp
// Author      :
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <iostream>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include "colormod.hpp" // namespace Color
#include <stdio.h>
#include <stdarg.h>

#include <stdio.h>
#include <chrono>
#include <ctime>
#include <time.h>
#include <semaphore.h>

#include <libcec/cec.h>
#include <libcec/cecloader.h>
#include <libcec/cectypes.h>

#include "MQTTClient.h"

#define DEBUG 0
#define COLORPRINT 1

using namespace std;
using namespace CEC;

sem_t mutex;

// MQTT Variables
char ADDRESS[] = "tcp://192.168.1.26:1883";
char CLIENTID[] = "Ambilight RPI";
char Power[] = "rpi/ambilight/control";
char Color[] = "rpi/ambilight/color";

char sendColor[7] = "FF9C00";
#define QOS         1
#define TIMEOUT     10000L

volatile MQTTClient_deliveryToken deliveredtoken;
volatile bool mqttEnableHyperion;
volatile bool enableReconnect;
volatile bool colorUpdateAvailable;

// CEC Variables
ICECCallbacks g_callbacks;
libcec_configuration g_config;
int g_cecLogLevel(-1);
int g_cecDefaultLogLevel(CEC_LOG_ALL);
ICECAdapter *g_parser;
bool g_bSingleCommand(false);
std::string g_strPort;

void logInfo(uint8_t colorSelektion, char const *message, ...) {
	va_list args;
	char temp[128] = { 0 };

	va_start(args, message);
	vsnprintf(temp, 128, message, args);
	va_end(args);

	auto currentTime = std::chrono::system_clock::now();
	std::time_t time = std::chrono::system_clock::to_time_t(currentTime);
	struct tm *actTime = gmtime(&time);

	Colormod::Modifier green(Colormod::FG_GREEN);
	Colormod::Modifier blue(Colormod::FG_BLUE);
	Colormod::Modifier red(Colormod::FG_RED);
	Colormod::Modifier def(Colormod::FG_DEFAULT);

	Colormod::Modifier textColor(Colormod::FG_DEFAULT);
	if (colorSelektion == 1) {
		textColor = green;
	} else if (colorSelektion == 2) {
		textColor = blue;
	} else if (colorSelektion == 3) {
		textColor = red;
	} else {
		textColor = def;
	}

#if COLORPRINT == 1
	cout << textColor;
	cout << "[" << actTime->tm_year << "-" << actTime->tm_mday << "-" << actTime->tm_mon << " ";
	cout << actTime->tm_hour << ":" << actTime->tm_min << ":" << actTime->tm_sec << "] - ";
	cout << temp;
	cout << def << endl;
#else
	cout << "[" << actTime->tm_year << "-" << actTime->tm_mday << "-" << actTime->tm_mon << " ";
	cout << actTime->tm_hour << ":" << actTime->tm_min << ":" << actTime->tm_sec << "] - ";
	cout << temp << endl;
#endif
}

void CecLogMessage(void *cbParam, const cec_log_message *message) {
	(void) cbParam;
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
		logInfo(0, "%s[%16lld]\t%s\n", strLevel.c_str(), message->time, message->message);
	}
}

void CecCommand(void *cbParam, const cec_command *command) {
	(void) cbParam;
	(void) command;
#if DEBUG == 1
	std::cout << "Command received" << std::endl;
	std::cout << command->initiator << std::endl;
	std::cout << command->destination << std::endl;
	std::cout << command->opcode << std::endl;
#endif
}

void delivered(void *context, MQTTClient_deliveryToken dt) {
	(void) context;
	logInfo(0, "Message with token value %d delivery confirmed\n", dt);
	deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
	(void) context;
	(void) topicLen;
	char red[3];
	char green[3];
	char blue[3];

	char temp[40];

	strncpy(temp, (char*) message->payload, message->payloadlen);
	temp[message->payloadlen] = '\0';

	logInfo(0, "Message arrived");
	logInfo(0, "     topic: %s", topicName);
	logInfo(0, "   message: %s", (char*) message->payload);
	if (strcmp(Power, (char*) topicName) == 0) {
		if (strcmp("true", (char*) message->payload) == 0) {
			mqttEnableHyperion = true;
		} else if (strcmp("false", (char*) message->payload) == 0) {
			mqttEnableHyperion = false;
		}
	} else if (strcmp(Color, (char*) topicName) == 0) {
		colorUpdateAvailable = true;
		sprintf(red, "%02x", (unsigned int) strtol(strtok((char*) temp, ","), NULL, 10));
		sprintf(green, "%02x", (unsigned int) strtol(strtok(NULL, ","), NULL, 10));
		sprintf(blue, "%02x", (unsigned int) strtol(strtok(NULL, ","), NULL, 10));
		sendColor[0] = red[0];
		sendColor[1] = red[1];
		sendColor[2] = green[0];
		sendColor[3] = green[1];
		sendColor[4] = blue[0];
		sendColor[5] = blue[1];
		sendColor[6] = '\0';
		logInfo(0, "Color: %s", sendColor);
	}

	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);

	sem_post(&mutex);
	return 1;
}

void connlost(void *context, char *cause) {
	(void) context;
	logInfo(3, "Connection lost");
	//std::cout << "     cause: " << cause << std::endl;
	enableReconnect = true;
}

int main() {
	sem_init(&mutex, 0, 0);
	struct timespec tm;

	logInfo(1, "hallo %d", 10);

	while (1)
		;

	/**********************************Init MQTT Client**********************************/

	MQTTClient client;
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
	int rc;

	MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	conn_opts.keepAliveInterval = 20;
	conn_opts.cleansession = 1;

	MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

	do {
		rc = MQTTClient_connect(client, &conn_opts);
		if (rc != MQTTCLIENT_SUCCESS) {
			logInfo(0, "Failed to connect, return code %d", rc);
			sleep(20);
		}
	} while (rc != MQTTCLIENT_SUCCESS);

	/**********************************Init CEC Client***********************************/
	g_config.Clear();
	g_callbacks.Clear();
	snprintf(g_config.strDeviceName, 13, "CECTester");
	g_config.clientVersion = LIBCEC_VERSION_CURRENT;
	g_config.bActivateSource = 0;
//	g_callbacks.logMessage = &CecLogMessage;
//	  g_callbacks.keyPress        = &CecKeyPress;
	g_callbacks.commandReceived = &CecCommand;
//	  g_callbacks.alert           = &CecAlert;
	g_config.callbacks = &g_callbacks;

	if (g_cecLogLevel == -1)
		g_cecLogLevel = g_cecDefaultLogLevel;

	if (g_config.deviceTypes.IsEmpty()) {
		if (!g_bSingleCommand)
			logInfo(0, "No device type given. Using 'TV device");
		g_config.deviceTypes.Add(CEC_DEVICE_TYPE_RECORDING_DEVICE);
	}

	g_parser = LibCecInitialise(&g_config);
	if (!g_parser) {
		logInfo(0, "Cannot load libcec.so");
		if (g_parser)
			UnloadLibCec(g_parser);
		return 1;
	}

// init video on targets that need this
	g_parser->InitVideoStandalone();

	if (!g_bSingleCommand) {
		logInfo(0, "CEC Parser created - libCEC version %s", g_parser->VersionToString(g_config.serverVersion).c_str());
	}

	/************************Autodetect device and open connection***********************/
	if (g_strPort.empty()) {
		if (!g_bSingleCommand)
			logInfo(0, "no serial port given. trying autodetect: ");
		cec_adapter_descriptor devices[10];
		uint8_t iDevicesFound = g_parser->DetectAdapters(devices, 10, NULL, true);
		if (iDevicesFound <= 0) {
			if (g_bSingleCommand)
				logInfo(3, "autodetect FAILED");
			else
				logInfo(3, "FAILED ");
			UnloadLibCec(g_parser);
			return 1;
		} else {
			if (!g_bSingleCommand) {
				logInfo(0, " path:      %s com port: %s", devices[0].strComPath, devices[0].strComName);
			}
			g_strPort = devices[0].strComName;
		}
	}

	logInfo(0, "opening a connection to the CEC adapter...");

	if (!g_parser->Open(g_strPort.c_str())) {
		logInfo(3, "unable to open the device on port %s", g_strPort.c_str());
		UnloadLibCec(g_parser);
		return 1;
	}

	/*******************************Subscrib to MQTT Topic*******************************/
	logInfo(0, "Subscribing to topic %s for client %s using Qos %d", Power, CLIENTID, QOS);
	MQTTClient_subscribe(client, Power, QOS);
	logInfo(0, "Subscribing to topic %s for client %s using Qos %d", Color, CLIENTID, QOS);
	MQTTClient_subscribe(client, Color, QOS);

	/**********************************Process behavior**********************************/
	bool loopEnable = true;
	int isOn = -1;
	int reconnectCounter = 0;
	cec_power_status iPower_old = cec_power_status::CEC_POWER_STATUS_UNKNOWN;
	while (loopEnable) {
		//----------------Check Time----------------
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

		//----------------Get power status but only if it's necessary----------------
		bool cecEnableHyperion = false;
//		if (timeEnableHyperion || mqttEnableHyperion) {
		if (mqttEnableHyperion) {
			cec_power_status iPower = g_parser->GetDevicePowerStatus((cec_logical_address) 0);
			if (iPower_old != iPower) {
				logInfo(0, "power status: %d", iPower);
				iPower_old = iPower;
			}

			if (iPower == cec_power_status::CEC_POWER_STATUS_ON || iPower == cec_power_status::CEC_POWER_STATUS_IN_TRANSITION_STANDBY_TO_ON) {
				cecEnableHyperion = true;
			} else if (iPower == cec_power_status::CEC_POWER_STATUS_STANDBY || iPower == cec_power_status::CEC_POWER_STATUS_IN_TRANSITION_ON_TO_STANDBY
					|| iPower == cec_power_status::CEC_POWER_STATUS_UNKNOWN) {
				cecEnableHyperion = false;
			}
		}

		//----------------Decide to power of or on hyperion----------------
//		if (cecEnableHyperion && (timeEnableHyperion || mqttEnableHyperion) && (isOn != 1)) {
		if (cecEnableHyperion && mqttEnableHyperion && (isOn != 1)) {
			system("hyperion-remote --clearall");
			system("hyperion-remote --luminanceMin 0.15");
			isOn = 1;
		} else if (mqttEnableHyperion && !cecEnableHyperion && (isOn != 2 || colorUpdateAvailable)) {
			char command[100];
			sprintf(command, "hyperion-remote --priority 0 --color %s", sendColor);
			system(command);
			isOn = 2;
			colorUpdateAvailable = false;
		} else if (!cecEnableHyperion && !mqttEnableHyperion && (isOn != 0)) {
			system("hyperion-remote --priority 0 --color black");
			system("hyperion-remote --luminanceMin 0.0");
			isOn = 0;
		}

		//----------------Try reconnet so MQTT server----------------
		if (enableReconnect) {
			logInfo(1, "Try reconnect %d", reconnectCounter);
			rc = MQTTClient_connect(client, &conn_opts);
			if (rc == MQTTCLIENT_SUCCESS) {
				logInfo(2, "Maybe reconnected, return code %d", rc);
				if (MQTTClient_isConnected(client)) {
					logInfo(1, "Safely reconnected, subscribe topics ", rc);
					logInfo(0, "Subscribing to topic %s for client %s using Qos %d", Power, CLIENTID, QOS);
					MQTTClient_subscribe(client, Power, QOS);
					logInfo(0, "Subscribing to topic %s for client %s using Qos %d", Color, CLIENTID, QOS);
					MQTTClient_subscribe(client, Color, QOS);
					enableReconnect = false;
					reconnectCounter = 0;
				}
			} else {
				logInfo(3, "Failed to connect, return code %d", rc);
				reconnectCounter++;
			}
		}

		// Avoid excessive load on CEC connection and time calculation
		clock_gettime(CLOCK_REALTIME, &tm);
		tm.tv_sec += 5;
		sem_timedwait(&mutex, &tm);
	}

	g_parser->Close();
	UnloadLibCec(g_parser);

	MQTTClient_unsubscribe(client, Power);
	MQTTClient_disconnect(client, 10000);
	MQTTClient_destroy(&client);

	return 0;
}
