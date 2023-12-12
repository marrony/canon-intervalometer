#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define BUFFER_SIZE 8

struct queue_t {
  int32_t buffer[BUFFER_SIZE];
  void *buffer_data[BUFFER_SIZE];
  int32_t size;
  int32_t nextin;
  int32_t nextout;
  pthread_mutex_t mutex;
  pthread_cond_t produced;
  pthread_cond_t consumed;
};

#define QUEUE_INITIALIZER                                                      \
  {                                                                            \
    .buffer = {0}, .buffer_data = {0}, .size = 0, .nextin = 0, .nextout = 0,   \
    .mutex = PTHREAD_MUTEX_INITIALIZER, .produced = PTHREAD_COND_INITIALIZER,  \
    .consumed = PTHREAD_COND_INITIALIZER                                       \
  }

int32_t queue_enqueue(struct queue_t *b, int32_t cmd, void *data);
int32_t queue_dequeue(struct queue_t *b, int32_t *cmd, void **data,
                      int64_t timer_ns);

struct sync_queue_t {
  struct queue_t queue;
  pthread_mutex_t sync_mutex;
  pthread_cond_t sync_wait;
  uint32_t processed;
};

void async_queue_unlock(struct sync_queue_t *queue, int32_t slot);
int32_t async_queue_dequeue_locked(struct sync_queue_t *queue, int32_t *cmd,
                                   void **data, int64_t timer_ns);
void async_queue_post(struct sync_queue_t *queue, int32_t cmd, void *data,
                      bool async);

#endif // QUEUE_H
