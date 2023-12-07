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

struct start_shooting_cmd {
  long delay;
  long exposure;
  long interval;
  long frames;
};

struct command_t {
  enum command_type type;
  union {
    struct start_shooting_cmd start_shooting;
  } cmd_data;
};

#endif // COMMAND_H
