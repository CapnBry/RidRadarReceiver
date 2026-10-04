#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "rid_queue.h"

#define RID_QUEUE_DEPTH 16

static QueueHandle_t ridQueue;
static volatile uint32_t ridDropped;

void rid_queue_init()
{
    ridQueue = xQueueCreate(RID_QUEUE_DEPTH, sizeof(RidRawMessage));
}

static bool rid_queue_send(RidRawMessage *m)
{
    // Called from the radio tasks, never block them
    if (xQueueSend(ridQueue, m, 0) == pdTRUE)
        return true;
    ++ridDropped;
    return false;
}

bool rid_queue_push_single(RidSource source, const uint8_t mac[6], int8_t rssi, const uint8_t *msg)
{
    RidRawMessage m;
    m.source = source;
    m.rssi = rssi;
    memcpy(m.mac, mac, sizeof(m.mac));
    m.count = 1;
    memcpy(m.msgs[0], msg, ODID_MESSAGE_SIZE);
    return rid_queue_send(&m);
}

bool rid_queue_push_pack(RidSource source, const uint8_t mac[6], int8_t rssi, const uint8_t *data, size_t len)
{
    constexpr size_t hdrLen = offsetof(ODID_MessagePack_encoded, Messages);
    if (len < hdrLen)
        return false;

    const ODID_MessagePack_encoded *pack = (const ODID_MessagePack_encoded *)data;
    if (pack->MessageType != ODID_MESSAGETYPE_PACKED
        || pack->SingleMessageSize != ODID_MESSAGE_SIZE
        || pack->MsgPackSize == 0
        || pack->MsgPackSize > ODID_PACK_MAX_MESSAGES
        || hdrLen + pack->MsgPackSize * ODID_MESSAGE_SIZE > len)
        return false;

    RidRawMessage m;
    m.source = source;
    m.rssi = rssi;
    memcpy(m.mac, mac, sizeof(m.mac));
    m.count = pack->MsgPackSize;
    memcpy(m.msgs, pack->Messages, m.count * ODID_MESSAGE_SIZE);
    return rid_queue_send(&m);
}

bool rid_queue_receive(RidRawMessage *msg)
{
    return xQueueReceive(ridQueue, msg, 0) == pdTRUE;
}

uint32_t rid_queue_dropped()
{
    return ridDropped;
}
