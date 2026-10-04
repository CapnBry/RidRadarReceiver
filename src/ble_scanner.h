#pragma once

// Start a continuous passive BLE scan for Bluetooth 4 legacy RemoteID
// advertisements, received messages are pushed to the rid_queue
void ble_scanner_init();
