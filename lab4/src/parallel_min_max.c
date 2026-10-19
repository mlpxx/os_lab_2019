#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <getopt.h>

#include "find_min_max.h"
#include "utils.h"

// Выставляется обработчиком SIGALRM, когда истёк таймаут.
static volatile sig_atomic_t timeout_expired = 0;

static void OnAlarm(int sig) {
  (void)sig;
  timeout_expired = 1;
}

int main(int argc, char **argv) {
  int seed = -1;
  int array_size = -1;
  int pnum = -1;
  int timeout = 0;  // 0 - таймаут не задан
  bool with_files = false;

  while (true) {
    static struct option options[] = {{"seed", required_argument, 0, 0},
                                      {"array_size", required_argument, 0, 0},
                                      {"pnum", required_argument, 0, 0},
                                      {"by_files", no_argument, 0, 'f'},
                                      {"timeout", required_argument, 0, 0},
                                      {0, 0, 0, 0}};

    int option_index = 0;
    int c = getopt_long(argc, argv, "f", options, &option_index);

    if (c == -1) break;

    switch (c) {
      case 0:
        switch (option_index) {
          case 0:
            seed = atoi(optarg);
            if (seed <= 0) {
              printf("seed must be a positive number\n");
              return 1;
            }
            break;
          case 1:
            array_size = atoi(optarg);
            if (array_size <= 0) {
              printf("array_size must be a positive number\n");
              return 1;
            }
            break;
          case 2:
            pnum = atoi(optarg);
            if (pnum <= 0) {
              printf("pnum must be a positive number\n");
              return 1;
            }
            break;
          case 3:
            with_files = true;
            break;
          case 4:
            timeout = atoi(optarg);
            if (timeout <= 0) {
              printf("timeout must be a positive number of seconds\n");
              return 1;
            }
            break;

          default:
            printf("Index %d is out of options\n", option_index);
        }
        break;
      case 'f':
        with_files = true;
        break;

      case '?':
        break;

      default:
        printf("getopt returned character code 0%o?\n", c);
    }
  }

  if (optind < argc) {
    printf("Has at least one no option argument\n");
    return 1;
  }

  if (seed == -1 || array_size == -1 || pnum == -1) {
    printf(
        "Usage: %s --seed \"num\" --array_size \"num\" --pnum \"num\" "
        "[--timeout \"sec\"] [--by_files]\n",
        argv[0]);
    return 1;
  }

  int *array = malloc(sizeof(int) * array_size);
  if (array == NULL) {
    printf("Failed to allocate memory for the array\n");
    return 1;
  }
  GenerateArray(array, array_size, seed);
  int active_child_processes = 0;

  // pipefd[i] - канал i-го ребёнка (используется, если нет --by_files).
  int pipefd[pnum][2];
  // PID каждого ребёнка (0 - уже дождались) и признак, что он завершился сам.
  pid_t child_pids[pnum];
  bool child_ok[pnum];

  // Обработчик ставим до fork: SIGALRM придёт только родителю
  // (таймер alarm в дочерний процесс не наследуется).
  if (timeout > 0) {
    signal(SIGALRM, OnAlarm);
    alarm(timeout);
  }

  struct timeval start_time;
  gettimeofday(&start_time, NULL);

  for (int i = 0; i < pnum; i++) {
    child_pids[i] = 0;
    child_ok[i] = true;

    if (!with_files && pipe(pipefd[i]) == -1) {
      perror("pipe");
      return 1;
    }

    pid_t child_pid = fork();
    if (child_pid >= 0) {
      // successful fork
      active_child_processes += 1;
      if (child_pid == 0) {
        // child process

#ifdef CHILD_DELAY
        // Только для демонстрации таймаута: ребёнок "долго считает".
        sleep(CHILD_DELAY);
#endif

        unsigned int begin = (unsigned long long)i * array_size / pnum;
        unsigned int end = (unsigned long long)(i + 1) * array_size / pnum;
        struct MinMax local = GetMinMax(array, begin, end);

        if (with_files) {
          char filename[64];
          snprintf(filename, sizeof(filename), "min_max_%d.txt", i);
          FILE *f = fopen(filename, "w");
          if (f == NULL) {
            perror("fopen (child)");
            exit(1);
          }
          fprintf(f, "%d %d\n", local.min, local.max);
          fclose(f);
        } else {
          close(pipefd[i][0]);
          if (write(pipefd[i][1], &local, sizeof(local)) != sizeof(local)) {
            perror("write");
            exit(1);
          }
          close(pipefd[i][1]);
        }
        free(array);
        return 0;
      }

      // parent process
      child_pids[i] = child_pid;
      if (!with_files) {
        close(pipefd[i][1]);
      }

    } else {
      printf("Fork failed!\n");
      return 1;
    }
  }

  int killed_children = 0;

  if (timeout <= 0) {
    // Таймаут не задан - всё как раньше: блокирующее ожидание.
    while (active_child_processes > 0) {
      if (wait(NULL) == -1) {
        perror("wait");
        return 1;
      }
      active_child_processes -= 1;
    }
  } else {
    // Таймаут задан: ждём неблокирующе (WNOHANG) и по сигналу SIGALRM
    // посылаем всем ещё живым детям SIGKILL.
    bool kill_sent = false;
    while (active_child_processes > 0) {
      for (int i = 0; i < pnum; i++) {
        if (child_pids[i] == 0) continue;

        int status = 0;
        pid_t r = waitpid(child_pids[i], &status, WNOHANG);
        if (r == child_pids[i]) {
          // ребёнок завершился (сам или от SIGKILL) - "забираем" его
          child_ok[i] = WIFEXITED(status) && WEXITSTATUS(status) == 0;
          child_pids[i] = 0;
          active_child_processes -= 1;
        } else if (r == -1) {
          perror("waitpid");
          child_ok[i] = false;
          child_pids[i] = 0;
          active_child_processes -= 1;
        }
        // r == 0: этот ребёнок ещё работает
      }

      if (timeout_expired && !kill_sent) {
        kill_sent = true;
        for (int i = 0; i < pnum; i++) {
          if (child_pids[i] != 0) {
            kill(child_pids[i], SIGKILL);
            killed_children += 1;
          }
        }
        printf("Timeout (%d s) expired: sent SIGKILL to %d child process(es)\n",
               timeout, killed_children);
      }

      if (active_child_processes > 0) usleep(1000);  // не жжём CPU впустую
    }
    alarm(0);
  }

  struct MinMax min_max;
  min_max.min = INT_MAX;
  min_max.max = INT_MIN;
  int finished = 0;

  for (int i = 0; i < pnum; i++) {
    int min = INT_MAX;
    int max = INT_MIN;

    if (!child_ok[i]) {
      // Ребёнок убит по таймауту - результата у него нет.
      if (with_files) {
        char filename[64];
        snprintf(filename, sizeof(filename), "min_max_%d.txt", i);
        remove(filename);
      } else {
        close(pipefd[i][0]);
      }
      continue;
    }

    if (with_files) {
      char filename[64];
      snprintf(filename, sizeof(filename), "min_max_%d.txt", i);
            FILE *f = fopen(filename, "r");
      if (f == NULL) {
        perror("fopen (parent)");
        return 1;
      }
      if (fscanf(f, "%d %d", &min, &max) != 2) {
        printf("Failed to read result from %s\n", filename);
        fclose(f);
        return 1;
      }
      fclose(f);
      remove(filename);
    } else {
      struct MinMax local;
      if (read(pipefd[i][0], &local, sizeof(local)) != sizeof(local)) {
        printf("Failed to read result from pipe %d\n", i);
        return 1;
      }
      close(pipefd[i][0]);
      min = local.min;
      max = local.max;
    }
    finished += 1;

    if (min < min_max.min) min_max.min = min;
    if (max > min_max.max) min_max.max = max;
  }

  struct timeval finish_time;
  gettimeofday(&finish_time, NULL);

  double elapsed_time = (finish_time.tv_sec - start_time.tv_sec) * 1000.0;
  elapsed_time += (finish_time.tv_usec - start_time.tv_usec) / 1000.0;

  free(array);

  if (finished == 0) {
    printf("No child process finished before the timeout, no result\n");
    printf("Elapsed time: %fms\n", elapsed_time);
    return 1;
  }
  if (finished < pnum) {
    printf("Warning: result is based on %d of %d chunks only\n", finished,
           pnum);
  }

  printf("Min: %d\n", min_max.min);
  printf("Max: %d\n", min_max.max);
  printf("Elapsed time: %fms\n", elapsed_time);
  fflush(NULL);
  return 0;
}