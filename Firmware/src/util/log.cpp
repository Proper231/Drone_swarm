#include "util/log.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

#include "board/node_id.h"

const NodeId *Log::nodeId_ = nullptr;

void Log::bind(const NodeId &nodeId) { nodeId_ = &nodeId; }

void Log::event(const char *fmt, ...) {
  // Sized for the longest line there is, the once-a-second COUNTERS summary.
  char tail[224];
  va_list args;
  va_start(args, fmt);
  vsnprintf(tail, sizeof(tail), fmt, args);
  va_end(args);

  const unsigned node = (nodeId_ != nullptr) ? nodeId_->get() : 0u;
  Serial.printf("LOG,%u,%lu,%s\n", node, (unsigned long)millis(), tail);
}
