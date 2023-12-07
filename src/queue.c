#include "queue.h"
#include "timer.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

int32_t queue_enqueue(struct queue_t *b, const struct command_t *cmd) {
  assert(pthread_mutex_lock(&b->mutex) == 0);

  while (b->size >= BUFFER_SIZE)
    assert(pthread_cond_wait(&b->consumed, &b->mutex) == 0);

  assert(b->size < BUFFER_SIZE);

  int nextin = b->nextin++;
  b->nextin %= BUFFER_SIZE;
  b->size++;

  b->buffer[nextin] = *cmd;

  assert(pthread_cond_signal(&b->produced) == 0);
  assert(pthread_mutex_unlock(&b->mutex) == 0);

  return nextin;
}

int32_t queue_dequeue(struct queue_t *b, struct command_t *cmd,
                      int64_t timer_ns) {
  assert(pthread_mutex_lock(&b->mutex) == 0);

  struct timespec ts = {0, 0};
  adjust_timer_ns(&ts, timer_ns);

  while (b->size <= 0) {
    int ret = pthread_cond_timedwait(&b->produced, &b->mutex, &ts);
    if (ret == ETIMEDOUT) {
      assert(pthread_mutex_unlock(&b->mutex) == 0);
      return -1;
    }
  }

  assert(b->size > 0);

  int nextout = b->nextout++;
  b->nextout %= BUFFER_SIZE;
  b->size--;

  *cmd = b->buffer[nextout];

  assert(pthread_cond_signal(&b->consumed) == 0);
  assert(pthread_mutex_unlock(&b->mutex) == 0);

  return nextout;
}

void async_queue_unlock(struct sync_queue_t *queue, int32_t slot) {
  assert(pthread_mutex_lock(&queue->sync_mutex) == 0);
  queue->processed |= (1ul << slot);
  assert(pthread_cond_signal(&queue->sync_wait) == 0);
  assert(pthread_mutex_unlock(&queue->sync_mutex) == 0);
}

int32_t async_queue_dequeue(struct sync_queue_t *queue, struct command_t *cmd,
                            int64_t timer_ns) {
  return queue_dequeue(&queue->queue, cmd, timer_ns);
}

void async_queue_post(struct sync_queue_t *queue, enum command_type type,
                      size_t cmd_size, const void *cmd_data, bool async) {
  assert(pthread_mutex_lock(&queue->sync_mutex) == 0);

  struct command_t cmd = {
      .type = type,
  };

  if (cmd_data != NULL)
    memcpy(&cmd.cmd_data, cmd_data, cmd_size);

  int32_t slot = queue_enqueue(&queue->queue, &cmd);
  uint32_t mask = 1ul << slot;

  queue->processed &= ~mask;

  while (!async && (queue->processed & mask) == 0) {
    assert(pthread_cond_wait(&queue->sync_wait, &queue->sync_mutex) == 0);
  }

  assert(pthread_mutex_unlock(&queue->sync_mutex) == 0);
}
