/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include "mqtt_client.h"
#include "queue.h"

#if LWIP_NETCONN

static int inpub_id;
QueueHandle_t messagesToMqttConnect;

/**
 * C++ version 0.4 char* style "itoa":
 * Written by Lukás Chmela
 * Released under GPLv3.

 */
char* itoa(int value, char* result, int base) {
	// check that the base if valid
	if (base < 2 || base > 36) {
		*result = '\0';
		return result;
	}

	char* ptr = result, *ptr1 = result, tmp_char;
	int tmp_value;

	do {
		tmp_value = value;
		value /= base;
		*ptr++ =
				"zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35
						+ (tmp_value - value * base)];
	} while (value);

	// Apply negative sign
	if (tmp_value < 0)
		*ptr++ = '-';
	*ptr-- = '\0';
	while (ptr1 < ptr) {
		tmp_char = *ptr;
		*ptr-- = *ptr1;
		*ptr1++ = tmp_char;
	}
	return result;
}

/* Called when publish is complete either with sucess or failure */
void mqtt_pub_request_cb(void *arg, err_t result) {
	if (result != ERR_OK) {
		printf("Publish result: %d\n", result);
	}
}

// Using outgoing publish

void mqtt_client_publish(mqtt_client_t *client, void *arg) {
	err_t err = ERR_OK;

	messageMqtt_t *msg = (messageMqtt_t*) arg;
	if (msg != NULL)
		err = mqtt_publish(client, msg->topic, msg->payload,
				strlen(msg->payload), msg->qos, msg->retain,
				mqtt_pub_request_cb, NULL); //What's better, a general callback or unique callbacks?

	if (err != ERR_OK) {
		//Whenever we can not forward a message, it's because we were disconnected.
		NVIC_SystemReset(); /* If we could not connect after 10 attempts, we give up and reset*/
		mqtt_disconnect(client);
		mqtt_connection_cb(client, NULL, MQTT_CONNECT_DISCONNECTED);
		printf("Publish err: %d\n", err);
	}
}

// Implementing callbacks for incoming publish and data

/* The idea is to demultiplex topic and create some reference to be used in data callbacks
 Example here uses a global variable, better would be to use a member in arg
 If RAM and CPU budget allows it, the easiest implementation might be to just take a copy of
 the topic string and use it in mqtt_incoming_data_cb
 */

void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
	printf("Incoming publish payload with length %d, flags %u\n", len,
			(unsigned int) flags);

	if (flags & MQTT_DATA_FLAG_LAST) {
		/* Last fragment of payload received (or whole part if payload fits receive buffer
		 See MQTT_VAR_HEADER_BUFFER_LEN)  */

		/* Call function or do action depending on reference, in this case inpub_id */
		if (inpub_id == 0) {
			/* Don't trust the publisher, check zero termination */
			if (data[len - 1] == 0) {
				printf("mqtt_incoming_data_cb: %s\n", (const char *) data);
			}
		} else if (inpub_id == 1) {
			/* Call an 'A' function... */
		} else {
			printf("mqtt_incoming_data_cb: Ignoring payload...\n");
		}
	} else {
		/* Handle fragmented payload, store in buffer, write to file or whatever */
	}
}

void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len) {
	printf("Incoming publish at topic %s with total length %u\n", topic,
			(unsigned int) tot_len);

	/* Decode topic string into a user defined reference */
	if (strcmp(topic, "print_payload") == 0) {
		inpub_id = 0;
	} else if (topic[0] == 'A') {
		/* All topics starting with 'A' might be handled at the same way */
		inpub_id = 1;
	} else {
		/* For all other topics */
		inpub_id = 2;
	}
}

/*
 -----------------------------------------------------------------
 Implementing the subscription request status callback
 */

void mqtt_sub_request_cb(void *arg, err_t result) {
	/* Just print the result code here for simplicity,
	 normal behaviour would be to take some action if subscribe fails like
	 notifying user, retry subscribe or disconnect from server */
	printf("Subscribe result: %d\n", result);
}

/*-----------------------------------------------------------------------------------*/

static void mqtt_client_test_thread(void *arg) {

	//Cast arg into client
	mqtt_client_t *client = (mqtt_client_t*) arg;
	//If everything is ok, send stuff every second
	if (client != NULL) {
		uint32_t i = 0;
		char testPayload[20], numBuffer[10];
		memset(&numBuffer, 0, sizeof(numBuffer));
		messageMqtt_t msg = { 0, 0, "PW/V2/CIAA_NXP/NY/TEST", "TEST:CONNECTED" };
		vTaskDelay(1000 / portTICK_RATE_MS);
		mqtt_client_publish(client, (void*) &msg);
		while (1) {
			memset(&testPayload, 0, sizeof(testPayload));
			itoa(i, numBuffer, 10);
			msg.payload = strcat(testPayload, "TEST:");
			msg.payload = strcat(testPayload, numBuffer);
			mqtt_client_publish(client, (void*) &msg);
			vTaskDelay(1000 / portTICK_RATE_MS);
			i++;
		}
	}
}
/*-----------------------------------------------------------------------------------*/

static void mqtt_client_connection_thread(void *arg) {

	xTaskHandle mqtt_client_test_handler = NULL;
	/* Initialized to pdTRUE in order to allow a first run of the loop */
	BaseType_t rv = pdTRUE;
	/* MEF State*/
	mqttConnState_t mqttConnState = MQTT_IS_CONNECTING;
	/* Queue init*/
	messagesToMqttConnect = xQueueCreate(4, sizeof(mqtt_connection_status_t));
	/* Variable in which the queue values are received */
	mqtt_connection_status_t reportedStatus;

	uint8_t numConnectTries = 0;

	mqtt_client_t *client = mqtt_client_new();
	/* If allocation was ok, start monitoring the connection */
	if (client != NULL) {

		while (1) {
			switch (mqttConnState) {
			case MQTT_IS_CONNECTED: {
				/* We are where we want to be */
				/* Create test task after connection. Theoretically, messages can be sent at this point */
				numConnectTries = 0;
				mqtt_client_test_handler = sys_thread_new(
						"mqtt_client_test_thread", mqtt_client_test_thread,
						(void*) client,
						DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO + 1);
				break;
			}
			case MQTT_IS_CONNECTING: {
				/* If a test task was created, delete it before attempting reconnection */
				if (mqtt_client_test_handler != NULL)
					vTaskDelete(mqtt_client_test_handler);
				/* Let's try to connect */
				if (numConnectTries++ < MAX_CONNECT_TRIES)
					mqtt_client_do_connect(client);
				else
					NVIC_SystemReset(); /* If we could not connect after 10 attempts, we give up and reset*/
				break;
			}
			}

			/* Receive from connection callback the info to process */
			rv = xQueueReceive(messagesToMqttConnect, &reportedStatus,
					portMAX_DELAY);
			if (rv == pdTRUE) {
				if (reportedStatus == MQTT_CONNECT_ACCEPTED)
					mqttConnState = MQTT_IS_CONNECTED;
				else
					mqttConnState = MQTT_IS_CONNECTING;
			}
		}
	}
}

/*
 * -----------------------------------------------------------------
 * Implementing the "connection state change" callback
 */

void mqtt_connection_cb(mqtt_client_t *client, void *arg,
		mqtt_connection_status_t status) {
	err_t err;

	if (status == MQTT_CONNECT_ACCEPTED) {

		printf("mqtt_connection_cb: Successfully connected\n");

		/* Setup callback for incoming publish requests */
		mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb,
				mqtt_incoming_data_cb, arg);

		/* Subscribe to a topic named "subtopic" with QoS level 1, call mqtt_sub_request_cb with result */
		err = mqtt_subscribe(client, "PW/V2/CIAA_NXP/RQ/#", 0,
				mqtt_sub_request_cb, arg);
		if (err != ERR_OK) {
			printf("mqtt_subscribe return: %d\n", err);
		}

	} else {
		printf("mqtt_connection_cb: Disconnected, reason: %d\n", status);

		/* Its more nice to be connected, so try to reconnect */
		//mqtt_client_do_connect(client);
	}
	xQueueSend(messagesToMqttConnect, &status, portMAX_DELAY);
}

/*-----------------------------------------------------------------------------------*/
void mqtt_client_init(void) {

	sys_thread_new("mqtt_client_connection_thread",
			mqtt_client_connection_thread, NULL,
			DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO + 1);

}

/*
 Establish Connection with server
 */
void mqtt_client_do_connect(mqtt_client_t *client) {
	struct mqtt_connect_client_info_t ci;
	err_t err;

	/* Setup an empty client info structure */
	memset(&ci, 0, sizeof(ci));

	/* Minimal amount of information required is client identifier, so set it here */
	ci.client_id = "lwip_test";

	/* Initiate client and connect to server, if this fails immediately an error code is returned
	 otherwise mqtt_connection_cb will be called with connection result after attempting
	 to establish a connection with the server.
	 For now MQTT version 3.1.1 is always used */

	ip_addr_t ip_addr;

	IP4_ADDR(&ip_addr, 142, 93, 0, 227);

	err = mqtt_client_connect(client, &ip_addr, MQTT_PORT, mqtt_connection_cb,
	NULL, &ci);

	/* For now just print the result code if something goes wrong */
	if (err != ERR_OK) {
		printf("mqtt_connect return %d\n", err);
	}
}
/*-----------------------------------------------------------------------------------*/

#endif /* LWIP_NETCONN */
