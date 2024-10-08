#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <string.h>

#include "lownet.h"
#include "serial_io.h"

#include "utility.h"
#include "app_chat.h"

void chat_receive(const lownet_frame_t* frame) {
	if (frame->destination == lownet_get_device_id()) {
		// This is a tell message, just for us!
		char message[40 + frame->length]; 

		// Copy the payload from index 0 to frame->length into the message buffer
		sprintf(message, "Chat from %02X that is just for us: %.*s", frame->source, frame->length, frame->payload);

		serial_write_line(message);
	} else {
		// This is a broadcast shout message.
		char message[40 + frame->length]; 

		// Copy the payload from index 0 to frame->length into the message buffer
		sprintf(message, "Broadcast shout from %02X: %.*s", frame->source, frame->length, frame->payload);
		// Send the formatted message using serial_write_line
		serial_write_line(message);
	}
}

void chat_send(const char* message, uint8_t destination);

void chat_shout(const char* message) {
	chat_send(message, 0xFF);
}

void chat_tell(const char* message, uint8_t destination) {
	chat_send(message, destination);
}

void chat_send(const char* message, uint8_t destination){
	lownet_frame_t chat_frame;

	// Set magic bytes to identify the frame
	chat_frame.magic[0] = 0x10;
	chat_frame.magic[1] = 0x4E;

	// Set the source and destination IDs
	chat_frame.source = lownet_get_device_id(); // Your device ID
	chat_frame.destination = destination;  // Target device ID

	// Set the protocol to "chat"
	chat_frame.protocol = LOWNET_PROTOCOL_CHAT;

	// set payload length
	chat_frame.length = strlen(message);

	// Set padding bytes to zero
	chat_frame.padding[0] = 0;
	chat_frame.padding[1] = 0;

	// Ensure payload is cleared
	memset(chat_frame.payload, 0, LOWNET_PAYLOAD_SIZE);
	int i;
	for(i = 0; i < LOWNET_PAYLOAD_SIZE && message[i] != '\0'; i++){
		if(util_printable(message[i])){
			chat_frame.payload[i] = message[i];
		}
		else {
			chat_frame.payload[i] = ' '; // just put the space character if the char is not printable
		}
	}

	lownet_send(&chat_frame);
}
