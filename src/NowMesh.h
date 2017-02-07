#include <Arduino.h>
#include <functional>

extern "C" {
 #include <espnow.h>
 #include <user_interface.h>
}

// WiFi channel
#define CHANNEL 1

// Number of messages to remember
// If you have a very large mesh and/or very high message quantity,
//  you may want to increase STORED_MESSAGES.
#define STORED_MESSAGES 10
// Number of peers to be connected to.
// Any number can be connected to us.
// If you have trouble with messages not reaching their destination,
//  try increasing MAX_PEERS
#define MAX_PEERS 10

// Set NOWMESH_DEBUG to get debugging messages on Serial.
// Each level includes those below it.
#define NOWMESH_DEBUG 0
#define LEVEL_UNLIKELY_ERROR 1
#define LEVEL_ERROR 2
#define LEVEL_NORMAL 3

// Maximum message length
//
// Message lengths. Add 1 for each character of message sent.
//            Minimum    Typical     Maximum
// broadcast: 28         36          44          
// targeted:  28         44          56
#define MAX_MSG_LEN 65

struct message_info {
 uint8_t originator[6];
 uint8_t sender[6];
 uint16_t id;
};

struct peer_info {
 uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
 int16_t score = 0;
};

class NowMesh {
protected:
 static message_info message_store[STORED_MESSAGES];

 static std::function<void(String, bool, uint8_t*)> receiveCallback;
 static std::function<void(int)> sendCallback; 
 
 static void ICACHE_FLASH_ATTR scanDoneCallback(void* arg, STATUS status);
 static void ICACHE_FLASH_ATTR receiveData(unsigned char* mac, unsigned char* data, uint8_t len);
 static void ICACHE_FLASH_ATTR sendData(unsigned char* mac_addr, unsigned char status); 

 static void ICACHE_FLASH_ATTR nowmeshDebug(String message, int level);
 
 static int ICACHE_FLASH_ATTR sendMessage(uint8_t* target, char* data);
 static int ICACHE_FLASH_ATTR sendBroadcast(String message, uint8_t* originator, uint16_t message_id);
 static int ICACHE_FLASH_ATTR sendTargeted(String message, uint8_t* originator, uint8_t* target, uint16_t message_id);

private:
 uint16_t last_message_id = 0;
  
public:
 NowMesh();
 void ICACHE_FLASH_ATTR begin();
 void ICACHE_FLASH_ATTR setReceiveCallback(std::function<void(String, bool, uint8_t*)> callback);
 void ICACHE_FLASH_ATTR setSendCallback(std::function<void(int)> callback);
 void ICACHE_FLASH_ATTR scanForPeers();
 void ICACHE_FLASH_ATTR send(String message);
 void ICACHE_FLASH_ATTR send(String message, uint8_t* target);
};
