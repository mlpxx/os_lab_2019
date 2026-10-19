#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <sys/time.h>

#include "sum.h"
#include "utils.h"

// Поток возвращает свою частичную сумму через void* (её забирает pthread_join).
_Static_assert(sizeof(void *) >= sizeof(uint64_t),
               "pointer must be able to hold a 64-bit partial sum");

void *ThreadSum(void *args) {
  const struct SumArgs *sum_args = (const struct SumArgs *)args;
  return (void *)(uintptr_t)Sum(sum_args);
}

// Разбор положительного числа не больше max. Возвращает 1 при успехе.
static int ParseNumber(const char *s, uint32_t max, uint32_t *out) {
  char *end = NULL;
  errno = 0;
  unsigned long v = strtoul(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0' || v == 0 || v > max) return 0;
  *out = (uint32_t)v;
  return 1;
}

static void PrintUsage(const char *prog) {
  printf("Usage: %s --threads_num \"num\" --seed \"num\" --array_size \"num\"\n",
         prog);
}

int main(int argc, char **argv) {
  uint32_t threads_num = 0;
  uint32_t array_size = 0;
  uint32_t seed = 0;

  static struct option options[] = {{"threads_num", required_argument, 0, 0},
                                    {"seed", required_argument, 0, 0},
                                    {"array_size", required_argument, 0, 0},
                                    {0, 0, 0, 0}};

  while (1) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "", options, &option_index);
    if (c == -1) break;
    if (c != 0) {  // неизвестная опция
      PrintUsage(argv[0]);
      return 1;
    }

    int ok = 0;
    switch (option_index) {
      case 0:
        ok = ParseNumber(optarg, 100000, &threads_num);
        break;
      case 1:
        ok = ParseNumber(optarg, UINT32_MAX, &seed);
        break;
      case 2:
        // GenerateArray использует int-индекс, поэтому не больше INT_MAX.
        ok = ParseNumber(optarg, INT_MAX, &array_size);
        break;
    }
    if (!ok) {
      printf("Invalid value for --%s: \"%s\" (must be a positive number)\n",
             options[option_index].name, optarg);
      return 1;
    }
  }

  if (optind < argc || threads_num == 0 || seed == 0 || array_size == 0) {
    PrintUsage(argv[0]);
    return 1;
  }

  int *array = malloc(sizeof(int) * (size_t)array_size);
  pthread_t *threads = malloc(sizeof(pthread_t) * threads_num);
  struct SumArgs *args = malloc(sizeof(struct SumArgs) * threads_num);
  if (array == NULL || threads == NULL || args == NULL) {
    printf("Error: not enough memory\n");
    return 1;
  }

  // Генерация массива - вне замера времени.
  GenerateArray(array, array_size, seed);

  struct timeval start_time;
  gettimeofday(&start_time, NULL);

  for (uint32_t i = 0; i < threads_num; i++) {
    // Каждому потоку - свой кусок массива [begin, end).
    args[i].array = array;
    args[i].begin = (uint32_t)((uint64_t)i * array_size / threads_num);
    args[i].end = (uint32_t)((uint64_t)(i + 1) * array_size / threads_num);

    if (pthread_create(&threads[i], NULL, ThreadSum, (void *)&args[i])) {
      printf("Error: pthread_create failed!\n");
      return 1;
    }
  }

  uint64_t total_sum = 0;
  for (uint32_t i = 0; i < threads_num; i++) {
    void *result = NULL;
    if (pthread_join(threads[i], &result)) {
      printf("Error: pthread_join failed!\n");
      return 1;
    }
    total_sum += (uint64_t)(uintptr_t)result;
  }

  struct timeval finish_time;
  gettimeofday(&finish_time, NULL);

  double elapsed_time = (finish_time.tv_sec - start_time.tv_sec) * 1000.0;
  elapsed_time += (finish_time.tv_usec - start_time.tv_usec) / 1000.0;

  free(args);
  free(threads);
  free(array);

  printf("Total: %" PRIu64 "\n", total_sum);
  printf("Elapsed time: %fms\n", elapsed_time);
  return 0;
}