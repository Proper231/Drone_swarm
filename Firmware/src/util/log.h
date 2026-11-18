// The one writer of the board-to-Pi log line (step 2.5):
//
//     LOG,<node>,<millis>,<event>,<key>=<value>,...
//
// Everything the Pi records for analysis goes through here, so the format is
// defined in exactly one place and cannot drift between modules.

#pragma once

class NodeId;

class Log {
 public:
  // Whose ID heads every line. It is read each time a line is written, so a
  // SETID takes effect on the very next event.
  static void bind(const NodeId &nodeId);

  // printf-style. The caller supplies everything after the millis field:
  //
  //     Log::event("NEIGHBOR_UP,node=%u,rssi=%d", id, rssi);
  static void event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

 private:
  static const NodeId *nodeId_;
};
