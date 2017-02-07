#include "Arduino.h"
#include "NowMesh.h"

// Scan every 5 seconds. If your nodes don't move around much or you don't
//  require high availability/reliability, you can increase this.
// Scans take around 3 seconds to complete, so don't go too low.
#define SCAN_INTERVAL 5000

// Send a message every second.
#define MESSAGE_INTERVAL 1000

// In this example sketch, we use a interval timer to scan.
os_timer_t scan_timer;
// The callback from the timer sets this boolean to true.
volatile bool should_scan = true;
// We'll do the same for our message timer.
os_timer_t message_timer;
// Only not attempt a message right away as we won't have any peers yet.
volatile bool should_message = false;

// Create mesh object.
NowMesh mesh;

// Here's the timer callback. Very simple. We'll check should_scan in loop().
void scanTimerCallback(void* arg) {
 should_scan = true;
}

// And the same for the message timer.
void messageTimerCallback(void* arg) {
 should_message = true;
}

// When we receive a message, whatever function we have set as receive callback will be called.
// The arguments are as follows:
// String    request          - the message
// bool      self_is_target   - true if the message was targeted and we are the intended recepient
// uint8_t*  originator       - MAC address of the node which originally sent the message
void messageReceivedCallback(String request, bool self_is_target, uint8_t* originator) {
 Serial.print("Received message");
 if (self_is_target) {
  Serial.print(" targeted at me");
 }
 Serial.print(" from ");
 for (int i = 0; i < 6; i++) {
  Serial.print(originator[i], HEX);
  Serial.print(":");
 }
 Serial.println(" " + request);
}

// When a message has been sent, whatever function we have set as sendcallback will be called.
// This does not necessarily indicate success. Check the status argument before assuming success.
void messageSendCallback(int status) {
 Serial.println("Message sent with status " + String(status, DEC));
}

void setup() {
 Serial.begin(115200);
 // Initialize the mesh. No arguments necessary.
 mesh.begin();
 // Receive and Send callbacks must be set.
 mesh.setReceiveCallback(messageReceivedCallback);
 mesh.setSendCallback(messageSendCallback);
 // Initialize the timers.
 os_timer_setfn(&scan_timer, scanTimerCallback, NULL);
 os_timer_arm(&scan_timer, SCAN_INTERVAL, true);
 os_timer_setfn(&message_timer, messageTimerCallback, NULL);
 os_timer_arm(&message_timer, MESSAGE_INTERVAL, true);
}

void loop() {
 // Check if the scan timer has fired.
 if (should_scan) {
  // Scan for peers. NowMesh will automatically connect to found peers.
  mesh.scanForPeers();
  should_scan = false;
 }
 // And if the message timer has fired
 if (should_message) {
  // Send a message.
  mesh.send("hi!");
  // You can also call NowMesh.send with two arguments,
  //  the second being the MAC address of the target.
  // uint8_t target = {0x0a, 0xdf, 0xae, 0x5c, 0x9d, 0x07};
  // mesh.send("hi", target);
 }
}
