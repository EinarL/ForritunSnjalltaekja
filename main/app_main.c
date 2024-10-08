// CSTDLIB includes.
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "driver/uart.h"
#include <ctype.h>
#include <esp_now.h>
#include <esp_wifi.h>

// LowNet includes.
#include "lownet.h"

#include "serial_io.h"
#include "utility.h"

#include "app_chat.h"
#include "app_ping.h"

#define UART_NUM UART_NUM_0
#define BUF_SIZE (1024)

const char* ERROR_OVERRUN = "ERROR // INPUT OVERRUN";
const char* ERROR_UNKNOWN = "ERROR // PROCESSING FAILURE";

const char* ERROR_COMMAND = "Command error";
const char* ERROR_ARGUMENT = "Argument error";

// Function to check if a string is a valid hexadecimal number
int isHexadecimal(const char* str) {
    int len = strlen(str);

	int start = 0;
    if (len > 2 && (str[0] == '0') && (str[1] == 'x' || str[1] == 'X')) {
        start = 2;
    }
	else return 0;

    // Check each character in the string, exluding the 0x at the beginning
    for (int i = start; i < len; i++) {
        if (!isxdigit((unsigned char)str[i])) {
            return 0;  // Return 0 if any character is not a valid hex digit
        }
    }
    return 1;  // Return 1 if all characters are valid hex digits
}

// Convert a valid hexadecimal string to uint8_t
uint8_t hexStringToUint8(const char* hexString) {
    return (uint8_t) strtol(hexString, NULL, 16);  // Convert string to uint8_t
}

// Function to convert uint8_t mac[6] to a human-readable string
void macToStr(const uint8_t mac[6], char* macStr) {
    // Use sprintf to format the MAC address into a string
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", 
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
// validates that a given hex string corresponds to some device id
// returns the device id if the hex string corresponds to a device id, 0 otherwise
uint8_t validateDeviceID(const char* hexString){
	if (isHexadecimal(hexString)) {
        // Convert to uint8_t
        uint8_t hexValue = hexStringToUint8(hexString);

		lownet_identifier_t deviceMacAndID = lownet_lookup(hexValue);
		if(deviceMacAndID.node == 0){
			serial_write_line("No device was found with that ID");
			return 0;
		}
		return deviceMacAndID.node;
	} else {
		serial_write_line("The given ID is not a valid hexadecimal number");
    }
	return 0;
}

void printDate(){
	lownet_time_t time = lownet_get_time();

	if(time.seconds == 0 && time.parts == 0){
		serial_write_line("Network time is not available.");
		return;
	}

	// Calculate fractional part (0-256) into milliseconds (0-999) with 10ms accuracy.
    float milliseconds = (time.parts * 1000.0f) / 256.0f;
    
    // Calculate the fraction representing 10ms precision.
    float fraction = ((int)(milliseconds / 10)) / 100.0f;

    // Print the formatted time with 10ms accuracy.
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.1f sec since the course started.", time.seconds + fraction);
    serial_write_line(buffer);
}

void app_frame_dispatch(const lownet_frame_t* frame) {
	printf("got here");
	switch(frame->protocol) {
		case LOWNET_PROTOCOL_RESERVE:
			// Invalid protocol, ignore.
			break;
		case LOWNET_PROTOCOL_TIME:
			// Not handled here.  Time protocol is special case
			break;
		case LOWNET_PROTOCOL_CHAT:
			chat_receive(frame);
			break;

		case LOWNET_PROTOCOL_PING:
			ping_receive(frame);
			break;
	}
}

void app_main(void)
{
	/*
	i know the program will behave unexpectedly if the input length is more than MSG_BUFFER_LENGTH,
	but i have programmed serial_console.py to make sure the input will always be less than 128 bits.
	 */
	char msg_in[MSG_BUFFER_LENGTH];
	char msg_out[MSG_BUFFER_LENGTH];

	// Initialize the serial services.
	init_serial_service();

	// Initialize the LowNet services.
	lownet_init(app_frame_dispatch);

	serial_write_line("> ");

	while (true) {
		memset(msg_in, 0, MSG_BUFFER_LENGTH);
		memset(msg_out, 0, MSG_BUFFER_LENGTH);

		if (!serial_read_line(msg_in)) {
			// Quick & dirty input parse.  Presume input length > 0.
			if (msg_in[0] == '/') {
				if (strncmp(&msg_in[1], "ping", 4) == 0){ // ping command
					// Create a new buffer for the substring, ensuring no overflow
					char id[MSG_BUFFER_LENGTH - 6];  // Enough to hold the remainder of the string
					
					// Copy the substring from index 6 to the end
					strncpy(id, &msg_in[6], sizeof(id) - 1);
					id[sizeof(id) - 1] = '\0'; // Ensure null termination

					uint8_t targetDeviceID = validateDeviceID(id);
					if(targetDeviceID){
						ping(targetDeviceID);
					}
				}
				else if (strncmp(&msg_in[1], "date", 4) == 0){ // date command
					printDate();
				}
			} else if (msg_in[0] == '@') {
				// Probably a chat 'tell' command.
				// Create a new buffer for the substring, ensuring no overflow
				char id[5];  // Enough to hold e.g. 0xAB + the null terminator
				// Copy the substring which is the id
				strncpy(id, &msg_in[1], 4);
				id[sizeof(id) - 1] = '\0'; // Ensure null termination
				uint8_t targetDeviceID = validateDeviceID(id);
				if(targetDeviceID){
					char msg[MSG_BUFFER_LENGTH];
					strncpy(msg, &msg_in[6], sizeof(msg) - 1);
					msg[sizeof(msg) - 1] = '\0'; // Ensure null termination

					chat_tell(msg, targetDeviceID);
				}
			} else {
				// Default, chat broadcast message.
				if(strlen(msg_in) > 0){
					msg_in[strlen(msg_in)] = '\0';
					chat_shout(msg_in);
				}
			}

			vTaskDelay(100 / portTICK_PERIOD_MS);
			serial_write_line("> ");
		}

		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}
