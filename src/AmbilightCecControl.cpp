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

#include "MQTTClient.h"

using namespace std;

using namespace CEC;

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

ICECCallbacks g_callbacks;
libcec_configuration g_config;
int g_cecLogLevel(-1);
int g_cecDefaultLogLevel(CEC_LOG_ALL);
ICECAdapter *g_parser;
bool g_bSingleCommand(false);
std::string g_strPort;

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
		printf("%s[%16lld]\t%s\n", strLevel.c_str(), message->time, message->message);
	}
}

void CecCommand(void *cbParam, const cec_command *command) {
	(void) cbParam;
	(void) command;
	std::cout << "Command received" << std::endl;
	std::cout << command->initiator << std::endl;
	std::cout << command->destination << std::endl;
	std::cout << command->opcode << std::endl;
}

void delivered(void *context, MQTTClient_deliveryToken dt) {
	(void) context;
	printf("Message with token value %d delivery confirmed\n", dt);
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

	std::cout << "Message arrived" << std::endl;
	std::cout << "     topic: " << topicName << std::endl;
	std::cout << "   message: " << (char*) message->payload << std::endl;
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
		std::cout << "Color: " << sendColor << std::endl;
	}

	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);
	return 1;
}

void connlost(void *context, char *cause) {
	(void) context;
	std::cout << "\nConnection lost\n";
	//std::cout << "     cause: " << cause << std::endl;
	enableReconnect = true;
}

int main() {

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
			std::cout << "Failed to connect, return code " << rc << std::endl;
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

	/************************Autodetect device and open connection***********************/
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

	/*******************************Subscrib to MQTT Topic*******************************/
	std::cout << "Subscribing to topic " << Power << " for client " << CLIENTID << " using QoS " << QOS << "\n" << std::endl;
	MQTTClient_subscribe(client, Power, QOS);
	std::cout << "Subscribing to topic " << Color << " for client " << CLIENTID << " using QoS " << QOS << "\n" << std::endl;
	MQTTClient_subscribe(client, Color, QOS);

	/**********************************Process behavior**********************************/
	bool loopEnable = true;
	int isOn = -1;
	int reconnectCounter = 0;
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
			std::cout << std::endl << "power status: " << g_parser->ToString(iPower) << std::endl;

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
			std::cout << "Try reconnect " << reconnectCounter << std::endl;
			rc = MQTTClient_connect(client, &conn_opts);
			if (rc == MQTTCLIENT_SUCCESS) {
				std::cout << "Maybe reconnected, return code " << rc << std::endl;
				if (MQTTClient_isConnected(client)) {
					std::cout << "Safely reconnected, subscribe topics " << std::endl;
					std::cout << "Subscribing to topic " << Power << " for client " << CLIENTID << " using QoS " << QOS << "\n" << std::endl;
					MQTTClient_subscribe(client, Power, QOS);
					std::cout << "Subscribing to topic " << Color << " for client " << CLIENTID << " using QoS " << QOS << "\n" << std::endl;
					MQTTClient_subscribe(client, Color, QOS);
					enableReconnect = false;
					reconnectCounter = 0;
				}
			} else {
				std::cout << "Failed to connect, return code " << rc << std::endl;
				reconnectCounter++;
			}
		}

		// Avoid excessive load on CEC connection and time calculation
		sleep(5);
	}

	g_parser->Close();
	UnloadLibCec(g_parser);

	MQTTClient_unsubscribe(client, Power);
	MQTTClient_disconnect(client, 10000);
	MQTTClient_destroy(&client);

	return 0;
}
