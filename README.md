# NowMesh
---

NowMesh is an easy-to-use mesh networking library for the ESP8266.
Utilizes ESP Now, a fast connectionless protocol.

See [examples/basic/basic.ino](https://github.com/chuckwagoncomputing/NowMesh/blob/master/examples/basic/basic.ino) for basic usage.

## Reliability
Tested with 11 nodes: Works perfectly
Tested with 31 nodes: STORED_MESSAGES in EspNow.h must be set to the number of nodes. Even then, several nodes gave problems.
