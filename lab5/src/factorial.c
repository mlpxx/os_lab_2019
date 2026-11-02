#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Параллельное вычисление k! по модулю mod.
// Пример запуска: ./factorial -k 10 --pnum=4 --mod=10

struct FactArgs {
  uint64_t begin;  // первое число диапазона (включительно)
  uint64_t end;    // последнее число диапазона (включительно)
  uint64_t mod;
};

// Общий результат, к которому обращаются все потоки.
static uint64_t result = 1;
static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;

// (a * b) % mod без переполнения: промежуточное произведение 128-битное.
static uint64_t MulMod(uint64_t a, uint64_t b, uint64_t mod) {
  return (uint64_t)((unsigned __int128)a * b % mod);
}

static void *ThreadFactorial(void *args) {
  const struct FactArgs *a = (const struct FactArgs *)args;

  // Каждый поток считает произведение своего диапазона независимо от других.
  uint64_t part = 1 % a->mod;
  for (uint64_t i = a->begin; i <= a->end; i++) {
    part = MulMod(part, i % a->mod, a->mod);
  }

  // Критическая секция: общий результат меняет только один поток за раз.
  pthread_mutex_lock(&mut);
  result = MulMod(result, part, a->mod);
  pthread_mutex_unlock(&mut);
  return NULL;
}

// Разбор неотрицательного числа. Возвращает 1 при успехе.
static int ParseNumber(const char *s, uint64_t *out) {
  if (*s == '-') return 0;  // strtoull молча превращает "-5" в огромное число
  char *end = NULL;
  errno = 0;
  unsigned long long v = strtoull(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0') return 0;
  *out = v;
  return 1;
}

static void PrintUsage(const char *prog) {
  printf("Usage: %s -k \"num\" --pnum=\"num\" --mod=\"num\"\n", prog);
}

int main(int argc, char **argv) {
  uint64_t k = 0, pnum = 0, mod = 0;
  int have_k = 0, have_pnum = 0, have_mod = 0;

  static struct option options[] = {{"k", required_argument, 0, 'k'},
                                    {"pnum", required_argument, 0, 'p'},
                                    {"mod", required_argument, 0, 'm'},
                                    {0, 0, 0, 0}};

  int c;
  while ((c = getopt_long(argc, argv, "k:", options, NULL)) != -1) {
    int ok = 1;
    switch (c) {
      case 'k':
        ok = ParseNumber(optarg, &k);
        have_k = 1;
        break;
      case 'p':
        ok = ParseNumber(optarg, &pnum);
        have_pnum = 1;
        break;
      case 'm':
        ok = ParseNumber(optarg, &mod);
        have_mod = 1;
        break;
      default:  // '?' - неизвестная опция
        PrintUsage(argv[0]);
        return 1;
    }
    if (!ok) {
      printf("Invalid value \"%s\": must be a non-negative integer\n", optarg);
      return 1;
    }
  }

  if (optind < argc || !have_k || !have_pnum || !have_mod) {
    PrintUsage(argv[0]);
    return 1;
  }
  if (pnum < 1 || pnum > 100000) {
    printf("pnum must be between 1 and 100000\n");
    return 1;
  }
  if (mod < 1) {
    printf("mod must be a positive number\n");
    return 1;
  }

  // Потоков больше, чем множителей, не нужно: лишние получили бы пустой диапазон.
  uint64_t threads_num = pnum;
  if (k > 0 && threads_num > k) threads_num = k;
  if (k == 0) threads_num = 1;  // 0! = 1, один поток с пустым диапазоном

  result = 1 % mod;

  pthread_t *threads = malloc(sizeof(pthread_t) * threads_num);
  struct FactArgs *args = malloc(sizeof(struct FactArgs) * threads_num);
  if (threads == NULL || args == NULL) {
    printf("Error: not enough memory\n");
    return 1;
  }

  // Числа 1..k делятся на threads_num почти равных кусков.
  // Например, k = 10, pnum = 4: [1..2] [3..5] [6..7] [8..10].
  uint64_t created = 0;
  for (uint64_t i = 0; i < threads_num; i++) {
    args[i].begin = (uint64_t)((unsigned __int128)i * k / threads_num) + 1;
    args[i].end = (uint64_t)((unsigned __int128)(i + 1) * k / threads_num);
    args[i].mod = mod;

    if (pthread_create(&threads[i], NULL, ThreadFactorial, &args[i]) != 0) {
      perror("pthread_create");
      break;
    }
    created++;
  }

  for (uint64_t i = 0; i < created; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      perror("pthread_join");
      return 1;
    }
  }
  if (created != threads_num) return 1;

  printf("%llu! mod %llu = %llu\n", (unsigned long long)k,
         (unsigned long long)mod, (unsigned long long)result);

  free(args);
  free(threads);
  return 0;
}