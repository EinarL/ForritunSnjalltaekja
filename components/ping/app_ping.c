#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_ping.h"

#include "serial_io.h"

typedef struct __attribute__((__packed__))
{
	lownet_time_t timestamp_out;
	lownet_time_t timestamp_back;
	uint8_t origin;
} ping_packet_t;

void ping(uint8_t node) {
	lownet_frame_t ping_frame;

	// Set magic bytes to identify the frame
	ping_frame.magic[0] = 0x10;
	ping_frame.magic[1] = 0x4E;

	// Set the source and destination IDs
	ping_frame.source = lownet_get_device_id(); // Your device ID
	ping_frame.destination = node;  // Target device ID
	printf("sending to %02X\n", ping_frame.destination);

	// Set the protocol to "ping"
	ping_frame.protocol = LOWNET_PROTOCOL_PING;
	printf("protocol: %u\n", ping_frame.protocol);
	// set payload length
	ping_frame.length = 11;

	// Set padding bytes to zero
	ping_frame.padding[0] = 0;
	ping_frame.padding[1] = 0;

	// Ensure payload is cleared
	memset(ping_frame.payload, 0, LOWNET_PAYLOAD_SIZE);
	/*
	lownet_time_t emptyTime = {0, 0};

	ping_packet_t pingPacket;
	pingPacket.timestamp_out = lownet_get_time();
	pingPacket.timestamp_back = emptyTime;
	pingPacket.origin = lownet_get_device_id();

	memcpy(ping_frame.payload, &pingPacket, sizeof(ping_packet_t));
	*/

	
	// Get the current time for the origin timestamp
	lownet_time_t origin_time = lownet_get_time();
	printf("Time - Seconds: %lu, Parts: %u\n", origin_time.seconds, origin_time.parts);
	
	// Copy the origin timestamp into the payload (first 5 bytes)
	memcpy(ping_frame.payload, &origin_time.seconds, 4);  // Copy 4 bytes of seconds
	ping_frame.payload[4] = origin_time.parts;                // Copy 1 byte of parts
	
	// Response timestamp will be 0 at the beginning, fill it as 5 bytes of 0
	memset(&ping_frame.payload[5], 0, 5);
	
	// Set the node ID at the 11th byte of the payload
	ping_frame.payload[10] = lownet_get_device_id();
	

	for (int i = 0; i < ping_frame.length; i++) {
    	printf("%02X ", ping_frame.payload[i]);
	}
	printf("\n");



	//ping_frame.crc = lownet_crc(&ping_frame);

	lownet_send(&ping_frame);
}

static void pong(const lownet_frame_t* frame) {
    // Create a new frame to send as a response
    lownet_frame_t pong_frame;

    // Set destination and source fields for the pong
    pong_frame.destination = frame->source;
    pong_frame.source = lownet_get_device_id();

    pong_frame.protocol = LOWNET_PROTOCOL_PING;

	ping_packet_t* pingPacket = (ping_packet_t*) frame->payload;
	pingPacket->timestamp_back = lownet_get_time(); // Get the current time for the response timestamp

	memcpy(pong_frame.payload, pingPacket, sizeof(ping_packet_t));

    pong_frame.length = 11;

    // Send the pong response
    lownet_send(&pong_frame);
}


/**
 * if the note id in the payload is the id of this device, then we know its a pong, 
 * otherwise its a ping and we need to send a pong back.
 */
void ping_receive(const lownet_frame_t* frame) {
	if (frame->length < sizeof(ping_packet_t)) {
		// Malformed frame.  Discard.
		serial_write_line("Malformed ping/pong");
		return;
	}
	
	ping_packet_t* pingPacket = (ping_packet_t*) frame->payload;

	if (pingPacket->origin == lownet_get_device_id()){ // if its a pong
		printf("It's a Pong");
		return;
	}
	printf("It's a Ping, origin device: %02X\n", pingPacket->origin);

	pong(frame);
}
