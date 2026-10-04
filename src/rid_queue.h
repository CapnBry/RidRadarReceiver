#pragma once

#include <stdint.h>
#include <stddef.h>
#include <opendroneid.h>

// Transport a RemoteID message was received on
enum RidSource : uint8_t {
    RID_SRC_WIFI_BEACON,
    RID_SRC_BLE4,
};

// Undecoded ODID messages from one received frame, passed from the radio
// tasks (WiFi / NimBLE host) to the main loop for decoding
typedef struct tagRidRawMessage {
    uint8_t source; // RidSource
    int8_t rssi;
    uint8_t mac[6];
    uint8_t count;  // number of valid entries in msgs
    uint8_t msgs[ODID_PACK_MAX_MESSAGES][ODID_MESSAGE_SIZE];
} RidRawMessage;

void rid_queue_init();
// Queue a single encoded ODID message (ODID_MESSAGE_SIZE bytes)
bool rid_queue_push_single(RidSource source, const uint8_t mac[6], int8_t rssi, const uint8_t *msg);
// Validate and queue an encoded ODID message pack of len bytes
bool rid_queue_push_pack(RidSource source, const uint8_t mac[6], int8_t rssi, const uint8_t *data, size_t len);
// Non-blocking receive, returns false if the queue is empty
bool rid_queue_receive(RidRawMessage *msg);
// Number of messages dropped because the queue was full
uint32_t rid_queue_dropped();
