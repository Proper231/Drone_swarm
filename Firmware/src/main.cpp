// Drone Swarm Failover - firmware entry point.
//
// One binary for every board; what makes a board D1 rather than D5 is a value
// in flash. Everything the board does lives in SwarmNode. Start reading at
// SwarmNode::update() in app/swarm_node.cpp.
//
// SAFETY: attach the LoRa antenna before any board transmits. The radio boots
// in receive-only mode and transmitting needs an explicit "PING ON".

#include <Arduino.h>

#include "app/swarm_node.h"

SwarmNode node;

void setup() { node.begin(); }

void loop() { node.update(); }
