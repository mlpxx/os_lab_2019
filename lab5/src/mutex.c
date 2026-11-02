/********************************************************
 * An example source module to accompany...
 *
 * "Using POSIX Threads: Programming with Pthreads"
 *     by Brad nichols, Dick Buttlar, Jackie Farrell
 *     O'Reilly & Associates, Inc.
 *  Modified by A.Kostin
 ********************************************************
 * mutex.c
 *
 * Simple multi-threaded example with a mutex lock.
 *
 * Сборка:
 *   без мьютекса: gcc mutex.c -o mutex_nolock -pthread
 *   с мьютексом:  gcc -DUSE_MUTEX mutex.c -o mutex_lock -pthread
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

void do_one_thing(int *);
void do_another_thing(int *);
void do_wrap_up(int);
int common = 0; /* A shared variable for two threads */
int r1 = 0, r2 = 0, r3 = 0;
pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;

/* С флагом -DUSE_MUTEX критическая секция защищена мьютексом,
 * без него LOCK() и UNLOCK() ничего не делают. */
#ifdef USE_MUTEX
#define LOCK() pthread_mutex_lock(&mut)
#define UNLOCK() pthread_mutex_unlock(&mut)
#else
#define LOCK()
#define UNLOCK()
#endif

int main() {
  pthread_t thread1, thread2;

  if (pthread_create(&thread1, NULL, (void *)do_one_thing,
			  (void *)&common) != 0) {
    perror("pthread_create");
    exit(1);
  }

  if (pthread_create(&thread2, NULL, (void *)do_another_thing,
                     (void *)&common) != 0) {
    perror("pthread_create");
    exit(1);
  }

  if (pthread_join(thread1, NULL) != 0) {
    perror("pthread_join");
    exit(1);
  }

  if (pthread_join(thread2, NULL) != 0) {
    perror("pthread_join");
    exit(1);
  }

  do_wrap_up(common);

  return 0;
}

void do_one_thing(int *pnum_times) {
  int i, j, x;
  unsigned long k;
  int work;
  for (i = 0; i < 50; i++) {
    LOCK();
    printf("doing one thing\n");
    work = *pnum_times;
    printf("counter = %d\n", work);
    work++; /* increment, but not write */
    for (k = 0; k < 500000; k++)
      ;                 /* long cycle */
    *pnum_times = work; /* write back */
    UNLOCK();
  }
}

void do_another_thing(int *pnum_times) {
  int i, j, x;
  unsigned long k;
  int work;
  for (i = 0; i < 50; i++) {
    LOCK();
    printf("doing another thing\n");
    work = *pnum_times;
    printf("counter = %d\n", work);
    work++; /* increment, but not write */
    for (k = 0; k < 500000; k++)
      ;                 /* long cycle */
    *pnum_times = work; /* write back */
    UNLOCK();
  }
}

void do_wrap_up(int counter) {
  int total;
  printf("All done, counter = %d\n", counter);
}