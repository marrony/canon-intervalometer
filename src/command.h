#ifndef COMMAND_H
#define COMMAND_H

enum command_type {
  NO_OP,
  INITIALIZE,
  DEINITIALIZE,
  CONNECT,
  DISCONNECT,
  TAKE_PICTURE,
  START_SHOOTING,
  STOP_SHOOTING,
  TERMINATE,
};

struct command_t {
  enum command_type type;
  union {
  } cmd_data;
};

#endif // COMMAND_H
