#ifndef GUARD_LOWNET_H
#define GUARD_LOWNET_H

#include <stdint.h>

#define LOWNET_SERVICE_CORE 1
#define LOWNET_SERVICE_PRIO 10
typedef enum  : uint8_t
{
	LOWNET_PROTOCOL_RESERVE = 0x00,
	LOWNET_PROTOCOL_TIME = 0x01,
	LOWNET_PROTOCOL_CHAT = 0x02,
	LOWNET_PROTOCOL_PING = 0x03,
} lownet_protocol_t;

#define LOWNET_FRAME_SIZE 212
#define LOWNET_HEAD_SIZE 8
#define LOWNET_CRC_SIZE 4
#define LOWNET_PAYLOAD_SIZE (LOWNET_FRAME_SIZE - (LOWNET_HEAD_SIZE + LOWNET_CRC_SIZE)) // 200 bytes.

typedef struct __attribute__((__packed__))
{
	uint8_t magic[2];
	uint8_t source;
	uint8_t destination;
	lownet_protocol_t protocol;
	uint8_t length;
	uint8_t padding[2];
	uint8_t payload[LOWNET_PAYLOAD_SIZE];
	uint32_t crc;
} lownet_frame_t;

typedef struct {
	uint32_t seconds; // Seconds since UNIX epoch.
	uint8_t parts; // Milliseconds, 1000/256 resolution.
} lownet_time_t;

typedef void (*lownet_recv_fn)(const lownet_frame_t* frame);

void lownet_init(lownet_recv_fn receive_cb);
void lownet_send(const lownet_frame_t* frame);

lownet_time_t lownet_get_time();
uint8_t lownet_get_device_id();

#include "lownet_util.h"


#endif
