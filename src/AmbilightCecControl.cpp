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

#include "MQTTClient.h"

#define DEBUG 0
#define COLORPRINT 1
#define USETIME 0

using namespace std;

sem_t mutex;

// MQTT Variables
char ADDRESS[] = "tcp://192.168.1.26:1883";
char CLIENTID[] = "Ambilight RPI";
char Power[] = "rpi/ambilight/control";
char Color[] = "rpi/ambilight/color";
char TvPowered[] = "rpi/ambilight/TvPowered";

char sendColor[7] = "FF9C00";
#define QOS         1
#define TIMEOUT     10000L

volatile MQTTClient_deliveryToken deliveredtoken;
volatile bool mqttEnableHyperion = false;
volatile bool mqttTvPowered = false;
volatile bool enableReconnect = false;
volatile bool colorUpdateAvailable = false;

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
	cout << "[" << (actTime->tm_year + 1900) << "-" << actTime->tm_mday << "-" << actTime->tm_mon << " ";
	cout << (actTime->tm_hour + 2) << ":" << actTime->tm_min << ":" << actTime->tm_sec << "] - ";
	cout << temp;
	cout << def << endl;
#else
	cout << "[" << actTime->tm_year << "-" << actTime->tm_mday << "-" << actTime->tm_mon << " ";
	cout << actTime->tm_hour << ":" << actTime->tm_min << ":" << actTime->tm_sec << "] - ";
	cout << temp << endl;
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
	logInfo(0, "topic: %s", topicName);
	logInfo(1, "message: %s", (char*) message->payload);
	if (strcmp(Power, (char*) topicName) == 0) {
		if (strcmp("true", (char*) message->payload) == 0) {
			mqttEnableHyperion = true;
		} else if (strcmp("false", (char*) message->payload) == 0) {
			mqttEnableHyperion = false;
		}
		logInfo(1, "Power MQTT set to: %s", (char*) message->payload);
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
	} else if (strcmp(TvPowered, (char*) topicName) == 0) {
		if (strcmp("true", (char*) message->payload) == 0) {
			mqttTvPowered = true;
		} else if (strcmp("false", (char*) message->payload) == 0) {
			mqttTvPowered = false;
		}
		logInfo(1, "TV Power is: %s", (char*) message->payload);
	} else {
		logInfo(1, "Unknown Message: %s", (char*) message->payload);
	}

	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);

	sem_post(&mutex);
	return 1;
}

void connlost(void *context, char *cause) {
	(void) context;
	(void) cause;
	logInfo(3, "Connection lost");
	//std::cout << "     cause: " << cause << std::endl;
	enableReconnect = true;
}

int main() {
	sem_init(&mutex, 0, 0);
	struct timespec tm;

	logInfo(1, "Starting");

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

	/*******************************Subscrib to MQTT Topic*******************************/
	logInfo(0, "Subscribing to topic %s for client %s using Qos %d", Power, CLIENTID, QOS);
	MQTTClient_subscribe(client, Power, QOS);
	logInfo(0, "Subscribing to topic %s for client %s using Qos %d", Color, CLIENTID, QOS);
	MQTTClient_subscribe(client, Color, QOS);
	logInfo(0, "Subscribing to topic %s for client %s using Qos %d", TvPowered, CLIENTID, QOS);
	MQTTClient_subscribe(client, TvPowered, QOS);

	/**********************************Process behavior**********************************/
	bool loopEnable = true;
	int isOn = -1;
	int reconnectCounter = 0;
	while (loopEnable) {
		//----------------Check Time----------------
#if USETIME == 1
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
#endif
//----------------Decide to power of or on hyperion----------------
#if USETIME == 1
		if (mqttTvPowered && (timeEnableHyperion || mqttEnableHyperion) && (isOn != 1)) {
#else
		if (mqttTvPowered && mqttEnableHyperion && (isOn != 1)) {
#endif
			system("hyperion-remote --clearall");
			system("hyperion-remote --luminanceMin 0.15");
			isOn = 1;
		} else if (mqttEnableHyperion && !mqttTvPowered && (isOn != 2 || colorUpdateAvailable)) {
			char command[100];
			sprintf(command, "hyperion-remote --priority 0 --color %s", sendColor);
			system(command);
			isOn = 2;
			colorUpdateAvailable = false;
		} else if (!mqttTvPowered && !mqttEnableHyperion && (isOn != 0)) {
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

	MQTTClient_unsubscribe(client, Power);
	MQTTClient_disconnect(client, 10000);
	MQTTClient_destroy(&client);

	return 0;
}
