#include <ctype.h>
#include <errno.h>
#include <limits.h>
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

int main(int argc, char **argv) {
  int seed = -1;
  int array_size = -1;
  int pnum = -1;
  bool with_files = false;

  while (true) {
    int current_optind = optind ? optind : 1;

    static struct option options[] = {{"seed", required_argument, 0, 0},
                                      {"array_size", required_argument, 0, 0},
                                      {"pnum", required_argument, 0, 0},
                                      {"by_files", no_argument, 0, 'f'},
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
    printf("Usage: %s --seed \"num\" --array_size \"num\" --pnum \"num\" [--by_files]\n",
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

  // По одному каналу на каждый дочерний процесс (используется, если нет --by_files).
  // pipefd[i][0] - конец для чтения, pipefd[i][1] - конец для записи.
  int pipefd[pnum][2];

  struct timeval start_time;
  gettimeofday(&start_time, NULL);

  for (int i = 0; i < pnum; i++) {
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

        // Каждый процесс обрабатывает свой кусок массива [begin, end).
        unsigned int begin = (unsigned long long)i * array_size / pnum;
        unsigned int end = (unsigned long long)(i + 1) * array_size / pnum;
        struct MinMax local = GetMinMax(array, begin, end);

        if (with_files) {
          // use files here
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
          // use pipe here
          close(pipefd[i][0]);  // ребёнок только пишет
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
      if (!with_files) {
        close(pipefd[i][1]);  // родитель только читает
      }

    } else {
      printf("Fork failed!\n");
      return 1;
    }
  }

  while (active_child_processes > 0) {
    // ждём завершения любого дочернего процесса
    if (wait(NULL) == -1) {
      perror("wait");
      return 1;
    }

    active_child_processes -= 1;
  }

  struct MinMax min_max;
  min_max.min = INT_MAX;
  min_max.max = INT_MIN;

  for (int i = 0; i < pnum; i++) {
    int min = INT_MAX;
    int max = INT_MIN;

    if (with_files) {
      // read from files
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
      remove(filename);  // временный файл больше не нужен
    } else {
      // read from pipes
      struct MinMax local;
      if (read(pipefd[i][0], &local, sizeof(local)) != sizeof(local)) {
        printf("Failed to read result from pipe %d\n", i);
        return 1;
      }
      close(pipefd[i][0]);
      min = local.min;
      max = local.max;
    }

    if (min < min_max.min) min_max.min = min;
    if (max > min_max.max) min_max.max = max;
  }

  struct timeval finish_time;
  gettimeofday(&finish_time, NULL);

  double elapsed_time = (finish_time.tv_sec - start_time.tv_sec) * 1000.0;
  elapsed_time += (finish_time.tv_usec - start_time.tv_usec) / 1000.0;

  free(array);

  printf("Min: %d\n", min_max.min);
  printf("Max: %d\n", min_max.max);
  printf("Elapsed time: %fms\n", elapsed_time);
  fflush(NULL);
  return 0;
}