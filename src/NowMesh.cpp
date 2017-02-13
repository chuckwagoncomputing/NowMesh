#include <NowMesh.h>

// A bit of info on how NowMesh works:
// There are two kinds of messages, broadcast and targeted.
// Nodes forward broadcast messages to all their peers unless they
//  have already received the message.
//  If you have trouble with messages that won't die, but keep being sent around,
//  increase STORED_MESSAGES in NowMesh.h
// When receiving a targeted message, nodes look through their stored messages in
//  order to find the best route. If they can't find a best route, they broadcast the message.
// Message format, each field separated by a comma:
// 1:  [1-2]      Message type. 1 = Broadcast, 2 = Targeted
// 2:  [0-255]    First byte of MAC address of the node that originated the message.
// 3:  [0-255]    Second
// 4:  [0-255]    Third
// 5:  [0-255]    Fourth
// 6:  [0-255]    Fifth
// 7:  [0-255]    Sixth
// 8:  [0-255]    First byte of MAC addresss of the target node, else 0 if message is broadcast.
// 9:  [0-255]
// 10: [0-255]
// 11: [0-255]
// 12: [0-255]
// 13: [0-255]
// 14: [0-65535]  Message ID. Each Node tracks their message ID, incrementing it every time they send a message.
// 15: ^[,]*      Message. This can be anything as long as it has no commas
//                 and the total message length is less than MAX_MSG_LEN, set in NowMesh.h

// This is where we store messages.
message_info NowMesh::message_store[STORED_MESSAGES];

// User facing callbacks for when we receive a message or a message has been sent.
// These callbacks won't get the whole message, only the part that was sent with NowMesh::send by the other node.
std::function<void(String, bool, uint8_t*)> NowMesh::receiveCallback;
std::function<void(int)> NowMesh::sendCallback; 

NowMesh::NowMesh() {
}

// Set callbacks
void ICACHE_FLASH_ATTR NowMesh::setReceiveCallback(std::function<void(String, bool, uint8_t*)> callback) {
 NowMesh::receiveCallback = callback;
}

void ICACHE_FLASH_ATTR NowMesh::setSendCallback(std::function<void(int)> callback) {
 NowMesh::sendCallback = callback;
}

// We scan for peers. User code should call NowMesh::scanForPeers every once in a while.
// scanForPeers is asynchrous, and this is its callback.
void ICACHE_FLASH_ATTR NowMesh::scanDoneCallback(void* arg, STATUS status) {
 // Make sure scan was successful.
 if (status == OK) {
  nowmeshDebug("Scan Done status OK", LEVEL_NORMAL);
  // We store found peers temporarily for processing.
  peer_info peer_store[MAX_PEERS];
  // Found AP info is in a tail queue; let's loop through it.
  struct bss_info* ap_link = (struct bss_info *)arg;
  while (ap_link != NULL) {
   String ssid = (const char*)ap_link->ssid;
   nowmeshDebug(String("Found AP: " + ssid), LEVEL_NORMAL);
   // Check for the default ESP8266 prefix so we don't try to peer with some random router.
   if (ssid.substring(0, 4) == "ESP_") {
    // Strategy here is to loop through stored peers and find an empty spot or replace the peer with the worst score.
    // candidate is the index of the one we want to replace
    int candidate = -1;
    int16_t worst_score = 0;
    // Score is based on signal strength
    int16_t score = 128 - abs(ap_link->rssi);
    // Loop through stored messages and add score for every message we have gotten from or through this peer.
    // This gives peers we have previously been in contact with an advantage.
    for (int i = 0; i < STORED_MESSAGES; i++) {
     if (memcmp(ap_link->bssid, message_store[i].originator, 6) == 0 || memcmp(ap_link->bssid, message_store[i].sender, 6) == 0) {
      score += 20;
     }
    }
    nowmeshDebug(String("AP score: " + String(score, DEC)), LEVEL_NORMAL);
    // Loop through the stored peers
    for (int i = 0; i < MAX_PEERS; i++) {
     // This place is empty, we'll just go ahead and store there.
     if (!(peer_store[i].score > 0)) {
      candidate = i;
      // Break from loop so we don't keep trying to find a place.
      break;
     }
     // If this peer has a worse score than we do and also breaks the record for worst score,
     //  it's the best candidate for replacement yet.
     if (peer_store[i].score < score && peer_store[i].score < worst_score) {
      candidate = i;
      // Set the record.
      worst_score = peer_store[i].score;
     }
    }
    // If we didn't find one to replace, candidate will still be -1.
    if (candidate >= 0) {
     nowmeshDebug(String("Storing in position " + String(candidate, DEC)), LEVEL_NORMAL);
     memcpy(peer_store[candidate].mac, ap_link->bssid, 6);
     peer_store[candidate].score = score;
    }
   }
   // Get the next AP in the tail queue
   ap_link = STAILQ_NEXT(ap_link, next);
  }
  // Now loop through peer storage. If they aren't already our peers, make them so.
  for (int i = 0; i < MAX_PEERS; i++) {
   if (peer_store[i].score > 0) {
    if (!esp_now_is_peer_exist(peer_store[i].mac)) {
     esp_now_add_peer(peer_store[i].mac, ESP_NOW_ROLE_SLAVE, CHANNEL, NULL, 0);
    }
   }
  }
  // Now loop through our peers to purge those we don't want to burden ourselves with.
  u8* peer = esp_now_fetch_peer(true);
  while (peer != NULL) {
   bool found = false;
   // Loop through peer storage to try to find them.
   for (int i = 0; i < MAX_PEERS; i++) {
    if (memcmp(peer, peer_store[i].mac, 6) == 0) {
     found = true;
    }
   }
   if (!found) {
    esp_now_del_peer(peer);
   }
   // Get the next peer.
   peer = esp_now_fetch_peer(false);
  }
 }
}

// User facing scan function, which should be called periodically.
void ICACHE_FLASH_ATTR NowMesh::scanForPeers() {
 // wifi_station_scan takes a config, which is nice because we can restrict it to only the channel which
 //  we know all peers will be using, saving time.
 struct scan_config config;
 config.ssid = NULL;
 config.bssid = NULL;
 config.channel = CHANNEL;
 wifi_station_scan(&config, scanDoneCallback);
}

// Send a message, any message...
// Used by sendBroadcast and sendTargeted
int ICACHE_FLASH_ATTR NowMesh::sendMessage(uint8_t* target, char* data){
 nowmeshDebug(String("Sending message out: " + String((const char*)data)), LEVEL_NORMAL);
 system_soft_wdt_feed();
 // If target is NULL, esp_now_send will send to all peers.
 return esp_now_send(target, reinterpret_cast<unsigned char*>(data), strlen(data));
}

// Send a broadcast message.
int ICACHE_FLASH_ATTR NowMesh::sendBroadcast(String message, uint8_t* originator, uint16_t message_id) {
 char data[MAX_MSG_LEN];
 sprintf(data, "1,%u,%u,%u,%u,%u,%u,0,0,0,0,0,0,%u,%s",
	 originator[0],
	 originator[1],
         originator[2],
	 originator[3],
	 originator[4],
	 originator[5],
	 message_id,
	 message.c_str()
 );
 return sendMessage(NULL, data);
}

// Send targeted message.
int ICACHE_FLASH_ATTR NowMesh::sendTargeted(String message, uint8_t* originator, uint8_t* target, uint16_t message_id) {
 uint8_t self[6];
 wifi_get_macaddr(0, self);
 char data[MAX_MSG_LEN];
 sprintf(data, "2,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%s",
	 originator[0],
	 originator[1],
	 originator[2],
	 originator[3],
	 originator[4],
	 originator[5],
	 target[0],
	 target[1],
	 target[2],
	 target[3],
	 target[4],
	 target[5],
	 message_id,
	 message.c_str()
 );
 // Loop through stored messages, looking for messages from the target.
 for (int i = 0; message_store[i].id > 0 && i < STORED_MESSAGES; i++) {
  // If this message originated from our target or was received directly from our target
  if (memcmp(message_store[i].originator, target, 6) == 0 || memcmp(message_store[i].sender, target, 6) == 0) {
   nowmeshDebug(String("Found stored message originating from or sent by the target of this message"), LEVEL_NORMAL);
   // If we are still peered with the sender of the message.
   if (esp_now_is_peer_exist(message_store[i].sender)) {
    // Send the message only to the sender.
    return sendMessage(message_store[i].sender, data);
   }
  }
 }
 // If control reaches this point, we didn't find any good route, so just broadcast the message.
 return sendMessage(NULL, data);
}

// Callback when we have received a message.
void ICACHE_FLASH_ATTR NowMesh::receiveData(unsigned char* mac, unsigned char* data, uint8_t len) {
 nowmeshDebug(String("Received raw: " + String((const char*)data)), LEVEL_NORMAL);
 nowmeshDebug(String("Receive length: " + String(len, DEC)), LEVEL_NORMAL);
 // If the message is too long, toss it out.
 // We don't wanna hang on a bad actor or transmission error.
 if (len > MAX_MSG_LEN) {
  nowmeshDebug("Bad message: too long", LEVEL_UNLIKELY_ERROR);
  return;
 }
 // Track field position
 int pos = 0;
 uint8_t message_type;
 uint8_t originator[6];
 uint8_t target[6];
 uint16_t message_id;
 // Each field is stored here
 char* token;
 // Message out.
 String message;
 // Message in. Cut it to length because it's not null terminated and there will be some garbage at the end.
 String received_message = String(reinterpret_cast<char*>(data)).substring(0, len);
 nowmeshDebug(String("Received cut: " + received_message), LEVEL_NORMAL);
 char received_buffer[MAX_MSG_LEN];
 strcpy(received_buffer, received_message.c_str());
 // Loop through one token at a time
 token = strtok(received_buffer, ",");
 while (token != NULL) {
  switch (pos) {
  case 0:
   message_type = String(token).toInt();
   break;
  case 1:
   originator[0] = String(token).toInt();
   break;
  case 2:
   originator[1] = String(token).toInt();
   break;
  case 3:
   originator[2] = String(token).toInt();
   break;
  case 4:
   originator[3] = String(token).toInt();
   break;
  case 5:
   originator[4] = String(token).toInt();
   break;
  case 6:
   originator[5] = String(token).toInt();
   break;
  case 7:
   target[0] = String(token).toInt();
   break;
  case 8:
   target[1] = String(token).toInt();
   break;
  case 9:
   target[2] = String(token).toInt();
   break;
  case 10:
   target[3] = String(token).toInt();
   break;
  case 11:
   target[4] = String(token).toInt();
   break;
  case 12:
   target[5] = String(token).toInt();
   break;
  case 13:
   message_id = String(token).toInt();
   break;
  case 14:
   message = token;
   break;
  }
  pos++;
  token = strtok(NULL, ",");
 }
 system_soft_wdt_feed();
 // If we had too many positions, give up. This will happen if there is a comma in the message sent!
 if (pos != 15) {
  nowmeshDebug("Bad Message: too many tokens", LEVEL_UNLIKELY_ERROR);
  return;
 }
 uint8_t self[6];
 wifi_get_macaddr(0, self);
 if (memcmp(originator, self, 6) == 0) {
  nowmeshDebug("We sent this message", LEVEL_NORMAL);
  return;
 }
 // Loop through stored messages and make sure we haven't seen this message already.
 // If we keep forwarding previously seen messages, the pipes will quickly clog.
 int msg_i = 0;
 while (msg_i < STORED_MESSAGES && message_store[msg_i].id > 0) {
  if (message_store[msg_i].id == message_id && memcmp(message_store[msg_i].originator, originator, 6) == 0) {
   nowmeshDebug("Message is already stored", LEVEL_NORMAL);
   return;
  }
  msg_i++;
 }
 nowmeshDebug(String("Stored Messages: " + String(msg_i, DEC)), LEVEL_NORMAL);
 // msg_i will hold the index of the first empty message.
 // If there are no empty messages, it will be the first address past the end of our array,
 //  so we need to store this message at the first index of the array and shift all messages to the back.
 if (msg_i == STORED_MESSAGES) {
  msg_i--;
 }
 // Loop through message store backwards and store the next message forward here.
 for (; msg_i > 0; msg_i--) {
  memcpy(message_store[msg_i].originator, message_store[msg_i - 1].originator, 6);
  memcpy(message_store[msg_i].sender, message_store[msg_i - 1].sender, 6);
  message_store[msg_i].id = message_store[msg_i - 1].id;
 }
 // Store this message as the first in the array.
 memcpy(message_store[0].originator, originator, 6);
 memcpy(message_store[0].sender, mac, 6);
 message_store[0].id = message_id;
 system_soft_wdt_feed();
 // If we are the target
 if (memcmp(target, self, 6) == 0) {
  // Call user facing received message callback.
  receiveCallback(message, false, originator);
 }
 else {
  // Resend message as necessary
  if (message_type == 1) {
   sendBroadcast(message, originator, message_id);
  }
  else if (message_type == 2) {
   sendTargeted(message, originator, target, message_id);
  }
  // Call user facing received message callback.
  receiveCallback(message, true, originator);
 }
}

// Callback for when message has been sent.
void ICACHE_FLASH_ATTR NowMesh::sendData(unsigned char* mac_addr, unsigned char status) {
 // We don't need to do any processing, just call the user facing callback.
 sendCallback(status);
}

// Debug function.
// Debug level settings are in NowMesh.h
void ICACHE_FLASH_ATTR NowMesh::nowmeshDebug(String message, int level) {
 if (NOWMESH_DEBUG >= level) {
  Serial.println(message);
 }
}

// User facing initialization function
void ICACHE_FLASH_ATTR NowMesh::begin() {
 nowmeshDebug("Starting NowMesh", LEVEL_NORMAL);
 // Set opmode as access point + station
 wifi_set_opmode(3);
 // Set channel
 wifi_set_channel(CHANNEL);
 // Initialize ESP Now and register callbacks
 if (esp_now_init() == 0) {
  nowmeshDebug("ESP Now init successful", LEVEL_NORMAL);
  esp_now_register_send_cb(reinterpret_cast<esp_now_send_cb_t>(&NowMesh::sendData));
  esp_now_register_recv_cb(reinterpret_cast<esp_now_recv_cb_t>(&NowMesh::receiveData));
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
 }
 else {
  nowmeshDebug("ESP Now init failed", LEVEL_ERROR);
 }
}

// User-facing send function for broadcast messages.
void ICACHE_FLASH_ATTR NowMesh::send(String message) {
 last_message_id++;
 uint8_t self[6];
 wifi_get_macaddr(0, self);
 sendBroadcast(message, self, last_message_id);
}

// User-facing send function for targeted messages.
void ICACHE_FLASH_ATTR NowMesh::send(String message, uint8_t* target) {
 last_message_id++;
 uint8_t self[6];
 wifi_get_macaddr(0, self);
 sendTargeted(message, self, target, last_message_id);
}
