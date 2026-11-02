#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Демонстрация deadlock (взаимной блокировки).
//
//   ./deadlock        - потоки берут два мьютекса в РАЗНОМ порядке: deadlock
//   ./deadlock --fix  - потоки берут мьютексы в ОДНОМ порядке: всё завершается

static pthread_mutex_t lock_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lock_b = PTHREAD_MUTEX_INITIALIZER;

static atomic_int finished = 0;  // сколько потоков дошло до конца
static int fix = 0;              // 1 - одинаковый порядок захвата

// Поток 1 всегда берёт сначала A, потом B.
static void *Thread1(void *arg) {
  (void)arg;
  pthread_mutex_lock(&lock_a);
  printf("thread 1: locked A, now wants B\n");
  sleep(1);  // даём второму потоку успеть захватить свой мьютекс
  pthread_mutex_lock(&lock_b);
  printf("thread 1: locked B too, working...\n");
  pthread_mutex_unlock(&lock_b);
  pthread_mutex_unlock(&lock_a);
  printf("thread 1: done\n");
  atomic_fetch_add(&finished, 1);
  return NULL;
}

// Поток 2: в режиме deadlock берёт сначала B, потом A (обратный порядок).
// В режиме --fix берёт в том же порядке, что и поток 1: сначала A, потом B.
static void *Thread2(void *arg) {
  (void)arg;
  pthread_mutex_t *first = fix ? &lock_a : &lock_b;
  pthread_mutex_t *second = fix ? &lock_b : &lock_a;
  const char *first_name = fix ? "A" : "B";
  const char *second_name = fix ? "B" : "A";

  pthread_mutex_lock(first);
  printf("thread 2: locked %s, now wants %s\n", first_name, second_name);
  sleep(1);
  pthread_mutex_lock(second);
  printf("thread 2: locked %s too, working...\n", second_name);
  pthread_mutex_unlock(second);
  pthread_mutex_unlock(first);
  printf("thread 2: done\n");
  atomic_fetch_add(&finished, 1);
  return NULL;
}

int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], "--fix") == 0) fix = 1;
  setvbuf(stdout, NULL, _IONBF, 0);  // сообщения сразу на экран

  printf("mode: %s\n", fix ? "same lock order (no deadlock)"
                           : "opposite lock order (deadlock)");

  pthread_t t1, t2;
  if (pthread_create(&t1, NULL, Thread1, NULL) != 0 ||
      pthread_create(&t2, NULL, Thread2, NULL) != 0) {
    perror("pthread_create");
    return 1;
  }

  // Сторожевой таймер: без него программа зависла бы навсегда.
  // Ждём до 5 секунд, пока оба потока завершатся.
  for (int i = 0; i < 50 && atomic_load(&finished) < 2; i++) {
    usleep(100000);
  }

  if (atomic_load(&finished) < 2) {
    printf("\nDEADLOCK: no progress for 5 seconds\n");
    printf("  thread 1 holds A and waits for B\n");
    printf("  thread 2 holds B and waits for A\n");
    printf("  nobody can continue, the program would hang forever\n");
    return 2;
  }

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);
  printf("\nboth threads finished, no deadlock\n");
  return 0;
}