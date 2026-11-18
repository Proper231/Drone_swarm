// The serial command interface, Pi to board (step 2.4).
//
// One line in, one action. This class owns the text side only: reading lines,
// checking arguments, printing "OK ..." or "ERR ...". What a command actually
// does to the board is a method on SwarmNode.
//
// The commands live in one table in the .cpp. HELP is printed from that table
// and usage errors are worded from it, so the three cannot drift apart.

#pragma once

#include <stddef.h>

class SwarmNode;

class CommandProcessor {
 public:
  explicit CommandProcessor(SwarmNode &node);

  // Call every loop. Reads whatever has arrived on serial and runs each
  // complete line.
  void update();

  void printHelp() const;

 private:
  // A handler returns false when its arguments were wrong, and the caller
  // then prints the usage line. Anything else, including an error it has
  // already reported itself, is true.
  using Handler = bool (CommandProcessor::*)(char *arg);

  struct Command {
    const char *name;
    const char *args;  // shown in HELP and in usage errors
    const char *help;
    Handler run;
  };

  static const Command kCommands[];
  static const size_t kCommandCount;

  void execute(char *line);

  bool cmdHelp(char *arg);
  bool cmdStatus(char *arg);
  bool cmdId(char *arg);
  bool cmdMac(char *arg);
  bool cmdSetId(char *arg);
  bool cmdClearId(char *arg);
  bool cmdProto(char *arg);
  bool cmdKill(char *arg);
  bool cmdRevive(char *arg);
  bool cmdJam(char *arg);
  bool cmdLoss(char *arg);
  bool cmdSplit(char *arg);
  bool cmdXband(char *arg);
  bool cmdHmac(char *arg);
  bool cmdHbJit(char *arg);
  bool cmdVoteJit(char *arg);
  bool cmdLora(char *arg);
  bool cmdPing(char *arg);
  bool cmdReset(char *arg);
  bool cmdReboot(char *arg);

  SwarmNode &node_;
  char line_[96];
  size_t lineLen_ = 0;
};
