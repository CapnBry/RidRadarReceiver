#include <NimBLEDevice.h>
#include "ble_scanner.h"
#include "rid_queue.h"

// ASTM F3411 RemoteID service data: UUID 0xFFFA, app code 0x0D, message counter, message
#define ODID_BLE_SERVICE_UUID   0xFFFA
#define ODID_BLE_APP_CODE       0x0D
#define ODID_BLE_HDR_LEN        2

// The radio is shared with WiFi, so only listen for part of each interval
#define BLE_SCAN_INTERVAL_MS    100
#define BLE_SCAN_WINDOW_MS      50

class RidScanCallbacks : public NimBLEScanCallbacks
{
    void onResult(const NimBLEAdvertisedDevice *dev) override
    {
        static const NimBLEUUID uuid((uint16_t)ODID_BLE_SERVICE_UUID);
        std::string sd = dev->getServiceData(uuid);
        if (sd.size() < ODID_BLE_HDR_LEN + ODID_MESSAGE_SIZE || (uint8_t)sd[0] != ODID_BLE_APP_CODE)
            return;

        // NimBLE stores the address little endian, flip to display order
        uint8_t mac[6];
        const uint8_t *addr = dev->getAddress().getVal();
        for (unsigned i=0; i<sizeof(mac); ++i)
            mac[i] = addr[sizeof(mac) - 1 - i];

        const uint8_t *msg = (const uint8_t *)sd.data() + ODID_BLE_HDR_LEN;
        size_t len = sd.size() - ODID_BLE_HDR_LEN;
        if (decodeMessageType(msg[0]) == ODID_MESSAGETYPE_PACKED)
            rid_queue_push_pack(RID_SRC_BLE4, mac, dev->getRSSI(), msg, len);
        else
            rid_queue_push_single(RID_SRC_BLE4, mac, dev->getRSSI(), msg);
    }

    void onScanEnd(const NimBLEScanResults &results, int reason) override
    {
        // Scan runs forever, but restart it if it ever stops
        NimBLEDevice::getScan()->start(0, false, true);
    }
};

static RidScanCallbacks scanCallbacks;

void ble_scanner_init()
{
    NimBLEDevice::init("");

    NimBLEScan *scan = NimBLEDevice::getScan();
    // Report every advertisement, RemoteID payloads change with the same address
    scan->setScanCallbacks(&scanCallbacks, true);
    scan->setActiveScan(false);
    scan->setInterval(BLE_SCAN_INTERVAL_MS);
    scan->setWindow(BLE_SCAN_WINDOW_MS);
    // Don't keep a results list, devices are deleted after the callback
    scan->setMaxResults(0);
    scan->start(0, false, true);
}
