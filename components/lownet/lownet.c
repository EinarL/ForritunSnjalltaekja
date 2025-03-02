#define INCLUDE_vTaskDelete 1

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <string.h>

#include "lownet.h"

#define TAG "lownet-core"

#define EVENT_CORE_READY 0x01
#define EVENT_CORE_ERROR 0x02

#define TIMEOUT_STARTUP ((TickType_t)(5000 / portTICK_PERIOD_MS))

struct {
	TaskHandle_t  service;

	EventGroupHandle_t events;
	QueueHandle_t inbound;
	lownet_recv_fn  dispatch;

	lownet_identifier_t identity;
	lownet_identifier_t broadcast;

	// Network timing details.
	lownet_time_t sync_time;
	int64_t sync_stamp;
} net_system;

const uint8_t lownet_magic[2] = {0x10, 0x4e};

uint8_t net_initialized = 0;

// Forward declarations.
void lownet_service_main(void* pvTaskParam);
void lownet_service_kill();
void lownet_inbound_handler(const esp_now_recv_info_t * info, const uint8_t* data, int len);

void lownet_sync_time(const lownet_frame_t* time_frame);


void lownet_init(lownet_recv_fn receive_cb) {
	if (net_initialized) {
		ESP_LOGE(TAG, "LowNet already initialized");
		return;
	} else {
		net_initialized = 1;
		memset(&net_system, 0, sizeof(net_system));
	}

	ESP_ERROR_CHECK(nvs_flash_init());        // initialize NVS
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());

	if (esp_now_init() != ESP_OK) {
		ESP_LOGE(TAG, "Error initializing ESP-NOW");
		return;
	}

	net_system.dispatch = receive_cb;

	net_system.events = xEventGroupCreate();
	if (!net_system.events) {
		ESP_LOGE(TAG, "Error creating lownet event group");
		return;
	}

	// Create the primary network service task.
	xTaskCreatePinnedToCore(
		lownet_service_main,
		"lownet_service",
		2048,
		NULL,				// Pass no params into task method.
		LOWNET_SERVICE_PRIO,
		&net_system.service,
		LOWNET_SERVICE_CORE
	);

	// Block until the service has finished its own startup and is ready to use.
	EventBits_t startup_result = xEventGroupWaitBits(
		net_system.events,
		(EVENT_CORE_READY | EVENT_CORE_ERROR),
		pdFALSE,
		pdFALSE,
		TIMEOUT_STARTUP
	);

	if (startup_result & EVENT_CORE_ERROR) {
		ESP_LOGE(TAG, "Error starting network service..");
		vEventGroupDelete(net_system.events);
		net_initialized = 0;
		return;
	}
	else if (!(startup_result & EVENT_CORE_READY)) {
		ESP_LOGE(TAG, "Timed out waiting for network service startup...");
		vEventGroupDelete(net_system.events);
		net_initialized = 0;
		return;
	}

	ESP_LOGI(TAG, "Initialized LowNet -- device ID: 0x%02X", net_system.identity.node);
}



void lownet_send(const lownet_frame_t* frame) {
	// Discard packet instead of sending if specified payload length
	// is impossible.
	if (frame->length > LOWNET_PAYLOAD_SIZE) { return; }
	
	lownet_frame_t out_frame;
	memset(&out_frame, 0, sizeof(out_frame));

	memcpy(&out_frame.magic, lownet_magic, 2);
	out_frame.source = net_system.identity.node; // Overwrite frame source with device ID.
	out_frame.destination = frame->destination;
	out_frame.protocol = frame->protocol;
	out_frame.length = frame->length;
	memcpy(out_frame.payload, frame->payload, frame->length);
	for (int i = frame->length; i < LOWNET_PAYLOAD_SIZE; ++i) {
		// Fill any unused payload with noise.  Improves packet entropy
		// for encryption purposes etc.
		out_frame.payload[i] |= (uint8_t)(esp_random() & 0x000000FF);
	}

	// Generate and apply the lownet CRC to the frame.
	out_frame.crc = lownet_crc(&out_frame);
	
	if (esp_now_send(net_system.broadcast.mac, (const uint8_t*)&out_frame, sizeof(out_frame)) != ESP_OK) {
		ESP_LOGE(TAG, "LowNet Frame send error");
	}
	else printf("sent\n");
}

// Formats and returns a lownet time structure based on synced network time.
lownet_time_t lownet_get_time() {
	lownet_time_t result;
	memset(&result, 0, sizeof(result));

	if (net_system.sync_time.seconds == 0) {
		// Haven't received a timesync yet.  Can't do anything useful.
		return result;
	}

	int64_t delta = ((esp_timer_get_time() / 1000) - net_system.sync_stamp)
					+ ((((int64_t)net_system.sync_time.parts) * 1000) / 256);

	result.seconds = net_system.sync_time.seconds + (uint32_t)(delta / 1000);
	result.parts = (uint8_t)(((delta % 1000) * 256) / 1000);

	return result;
}

// Returns the ID of this device.
uint8_t lownet_get_device_id() {
	return net_system.identity.node;
}


// NOTE: Task methods do not return as a rule of thumb; anywhere we
// encounter an error state we set the core error bit and kill the
// service.  Note we include a 'return;' after such a service kill,
// but these lines should never execute.
void lownet_service_main(void* pvTaskParam) {
	// Create an inbound packet queue.
	net_system.inbound = xQueueCreate(16, sizeof(lownet_frame_t));
	if (!net_system.inbound) {
		ESP_EARLY_LOGE(TAG, "Error creating lownet inbound packet queue");
		lownet_service_kill();
		return;
	}

	// Figure out our device identity, and the broadcast identity.
	uint8_t local_mac[6];
	esp_read_mac(local_mac, ESP_MAC_WIFI_STA);

	net_system.identity = lownet_lookup_mac(local_mac);
	net_system.broadcast = lownet_lookup(0xFF);

	if (!net_system.identity.node || !net_system.broadcast.node) {
		ESP_EARLY_LOGE(TAG, "Failed to identify device / broadcast identity");
		lownet_service_kill();
		return;
	}

	// Register the broadcast address.
	esp_now_peer_info_t peer_info = {};
	memcpy(peer_info.peer_addr, net_system.broadcast.mac, 6);
	peer_info.channel = 0;
	peer_info.ifidx = ESP_IF_WIFI_STA;
	peer_info.encrypt = false;
	if (esp_now_add_peer(&peer_info) != ESP_OK) {
		ESP_EARLY_LOGE(TAG, "Failed to add broadcast peer address");
		lownet_service_kill();
		return;
	}
	
	// Register our inbound network callback.
	esp_now_register_recv_cb(lownet_inbound_handler);

	// Initialization done.  Set the ready bit and then hang  around
	// dispatching frames as they arrive.
	xEventGroupSetBits(
		net_system.events,
		EVENT_CORE_READY
	);


	while (1) {
		lownet_frame_t  frame;
		memset(&frame, 0, sizeof(frame));

		// Blocking call to receive from inbound queue.  Task will be blocked until
		// queue has data for us.
		if (xQueueReceive(net_system.inbound, &frame, UINT32_MAX) == pdTRUE) {

			if (memcmp(frame.magic, lownet_magic, 2) != 0)
				continue;

			// Check whether the network frame checksum matches computed checksum.
			if (lownet_crc(&frame) != frame.crc) { continue; }

			// Not strictly to spec but a useful safety valve; if frame has, as a source
			// address, the broadcast address, discard it -- something has gone wrong.
			if (frame.source == 0xFF) { continue; }

			// Check whether packet destination is us or broadcast.
			if (frame.destination != net_system.identity.node && frame.destination != net_system.broadcast.node)  { continue; }

			switch(frame.protocol) {
				case LOWNET_PROTOCOL_RESERVE:
					// Reserved -- discard.
					break;

				case LOWNET_PROTOCOL_TIME:
					// TIME packet is a special case; handled entirely within lownet layer.
					lownet_sync_time(&frame);
					break;

				case LOWNET_PROTOCOL_CHAT:
				case LOWNET_PROTOCOL_PING:
					net_system.dispatch(&frame);
					break;

				default:
					// Unknown protocol -- discard.
					break;
			}
		}
	}
}

// Kills the lownet service task and allows for lownet re-initialization.
void lownet_service_kill() {
	xEventGroupSetBits(net_system.events, EVENT_CORE_ERROR);

	if (net_system.inbound) {
		vQueueDelete(net_system.inbound);
	}
	vTaskDelete(net_system.service);
	return; // Should never execute, when this function is called from lownet service.
}

// Inbound frame callback is executed from the context of the ESPNOW task!
// It is of great importance that this callback function not block, and
// return quickly to avoid locking up the wifi driver.
void lownet_inbound_handler(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
	//printf("recieved something");
	// Discard any frames that don't match the size of our network frame.
	if (len != sizeof(lownet_frame_t)) {
		return;
	}

	// Non-blocking queue send; if queue is full then packet is dropped.
	if (xQueueSend(net_system.inbound, data, 0) != pdTRUE) {
		// Error queueing data, likely errQUEUE_FULL.
		// Packet is dropped.
	}
}

void lownet_sync_time(const lownet_frame_t* time_frame) {
	if (time_frame->length != sizeof(lownet_time_t)) {
		// Malformed time packet, do nothing.
		return;
	}

	memcpy(&net_system.sync_time, time_frame->payload, sizeof(lownet_time_t));
	net_system.sync_stamp = (esp_timer_get_time() / 1000);
}
