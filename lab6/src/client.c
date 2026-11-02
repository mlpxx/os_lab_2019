#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "common.h"

struct Server {
  char ip[255];  // имя хоста в DNS - не длиннее 253 символов, плюс '\0'
  int port;
};

// Одно задание: посчитать произведение begin..end на одном сервере.
struct Job {
  struct Server server;
  uint64_t begin;
  uint64_t end;
  uint64_t mod;
  uint64_t answer;  // ответ сервера
  bool started;     // поток создан
  bool ok;          // ответ получен
};

// Читает файл, где в каждой строке записан сервер в виде ip:port.
// Пустые строки и строки, начинающиеся с '#', пропускаются.
// При ошибке возвращает NULL.
static struct Server *ReadServers(const char *path, unsigned int *count) {
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    fprintf(stderr, "Can not open servers file %s: %s\n", path,
            strerror(errno));
    return NULL;
  }

  struct Server *list = NULL;
  unsigned int n = 0;
  char *line = NULL;
  size_t cap = 0;
  unsigned int line_no = 0;
  bool ok = true;

  while (ok && getline(&line, &cap, f) != -1) {
    line_no++;

    // Убираем пробелы по краям и перевод строки (в том числе "\r\n"
    // из файлов, созданных в Windows).
    char *p = line;
    while (isspace((unsigned char)*p)) p++;
    size_t len = strlen(p);
    while (len > 0 && isspace((unsigned char)p[len - 1])) p[--len] = '\0';
    if (len == 0 || p[0] == '#') continue;

    char *colon = strrchr(p, ':');
    uint64_t port = 0;
    if (colon == NULL || colon == p) {
      fprintf(stderr, "%s:%u: expected ip:port\n", path, line_no);
      ok = false;
      break;
    }
    *colon = '\0';
    if (strlen(p) >= sizeof(list[0].ip) ||
        !ConvertStringToUI64(colon + 1, &port) || port < 1 || port > 65535) {
      fprintf(stderr, "%s:%u: bad server address\n", path, line_no);
      ok = false;
      break;
    }

    struct Server *bigger = realloc(list, sizeof(struct Server) * (n + 1));
    if (bigger == NULL) {
      fprintf(stderr, "Not enough memory\n");
      ok = false;
      break;
    }
    list = bigger;
    strcpy(list[n].ip, p);
    list[n].port = (int)port;
    n++;
  }

  free(line);
  fclose(f);

  if (ok && n == 0) {
    fprintf(stderr, "No servers found in %s\n", path);
    ok = false;
  }

  if (!ok) {
    free(list);
    return NULL;
  }
  *count = n;
  return list;
}

// Работа с одним сервером: соединиться, отправить задание, получить ответ.
// Выполняется в отдельном потоке, поэтому серверы работают одновременно.
static void *QueryServer(void *arg) {
  struct Job *job = (struct Job *)arg;
  job->ok = false;

  // getaddrinfo, а не gethostbyname: gethostbyname возвращает указатель
  // на общий статический буфер и небезопасна при вызове из нескольких потоков.
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", job->server.port);

  struct addrinfo *addr = NULL;
  int rc = getaddrinfo(job->server.ip, port_str, &hints, &addr);
  if (rc != 0) {
    fprintf(stderr, "%s:%d: address lookup failed: %s\n", job->server.ip,
            job->server.port, gai_strerror(rc));
    return NULL;
  }

  int sck = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
  if (sck < 0) {
    fprintf(stderr, "%s:%d: socket creation failed!\n", job->server.ip,
            job->server.port);
    freeaddrinfo(addr);
    return NULL;
  }

  if (connect(sck, addr->ai_addr, addr->ai_addrlen) < 0) {
    fprintf(stderr, "%s:%d: connection failed: %s\n", job->server.ip,
            job->server.port, strerror(errno));
    freeaddrinfo(addr);
    close(sck);
    return NULL;
  }
  freeaddrinfo(addr);

  char task[sizeof(uint64_t) * 3];
  memcpy(task, &job->begin, sizeof(uint64_t));
  memcpy(task + sizeof(uint64_t), &job->end, sizeof(uint64_t));
  memcpy(task + 2 * sizeof(uint64_t), &job->mod, sizeof(uint64_t));

  if (!SendAll(sck, task, sizeof(task))) {
    fprintf(stderr, "%s:%d: send failed\n", job->server.ip, job->server.port);
    close(sck);
    return NULL;
  }

  if (RecvAll(sck, &job->answer, sizeof(job->answer)) != 1) {
    fprintf(stderr, "%s:%d: receive failed\n", job->server.ip,
            job->server.port);
    close(sck);
    return NULL;
  }

  close(sck);
  job->ok = true;
  return NULL;
}

int main(int argc, char **argv) {
  uint64_t k = 0;
  uint64_t mod = 0;
  bool have_k = false;
  bool have_mod = false;
  // 255 - максимальная длина имени файла в Linux (NAME_MAX), так что обычный
  // путь помещается. Для очень длинных путей (PATH_MAX = 4096) места не хватит,
  // поэтому длину проверяем ниже, чтобы не выйти за границу массива.
  char servers[255] = {'\0'};

  while (true) {
    static struct option options[] = {{"k", required_argument, 0, 0},
                                      {"mod", required_argument, 0, 0},
                                      {"servers", required_argument, 0, 0},
                                      {0, 0, 0, 0}};

    int option_index = 0;
    int c = getopt_long(argc, argv, "", options, &option_index);

    if (c == -1)
      break;

    switch (c) {
    case 0: {
      switch (option_index) {
      case 0:
        if (!ConvertStringToUI64(optarg, &k)) {
          fprintf(stderr, "k must be a non-negative integer\n");
          return 1;
        }
        have_k = true;
        break;
      case 1:
        if (!ConvertStringToUI64(optarg, &mod) || mod < 1) {
          fprintf(stderr, "mod must be a positive integer\n");
          return 1;
        }
        have_mod = true;
        break;
      case 2:
        if (strlen(optarg) >= sizeof(servers)) {
          fprintf(stderr, "Path to the servers file is too long\n");
          return 1;
        }
        strcpy(servers, optarg);
        break;
      default:
        printf("Index %d is out of options\n", option_index);
      }
    } break;

    case '?':
      printf("Arguments error\n");
      return 1;
    default:
      fprintf(stderr, "getopt returned character code 0%o?\n", c);
    }
  }

  if (!have_k || !have_mod || !strlen(servers)) {
    fprintf(stderr, "Using: %s --k 1000 --mod 5 --servers /path/to/file\n",
            argv[0]);
    return 1;
  }

  unsigned int servers_num = 0;
  struct Server *to = ReadServers(servers, &servers_num);
  if (to == NULL) return 1;

  // 0! = 1, серверы для этого не нужны.
  if (k == 0) {
    printf("answer: %" PRIu64 "\n", 1 % mod);
    free(to);
    return 0;
  }

  // Серверов больше, чем чисел, не нужно: лишним нечего считать.
  unsigned int jobs_num = servers_num;
  if (k < jobs_num) jobs_num = (unsigned int)k;

  struct Job *jobs = calloc(jobs_num, sizeof(struct Job));
  pthread_t *threads = malloc(sizeof(pthread_t) * jobs_num);
  if (jobs == NULL || threads == NULL) {
    fprintf(stderr, "Not enough memory\n");
    return 1;
  }

  // Числа 1..k делятся между серверами почти поровну:
  // первые extra серверов берут на одно число больше.
  uint64_t chunk = k / jobs_num;
  uint64_t extra = k % jobs_num;
  uint64_t cur = 1;

  for (unsigned int i = 0; i < jobs_num; i++) {
    uint64_t len = chunk + (i < extra ? 1 : 0);
    jobs[i].server = to[i];
    jobs[i].begin = cur;
    jobs[i].end = cur + len - 1;
    jobs[i].mod = mod;
    cur += len;

    // Для каждого сервера свой поток: все серверы считают одновременно.
    if (pthread_create(&threads[i], NULL, QueryServer, &jobs[i]) != 0) {
      fprintf(stderr, "Error: pthread_create failed!\n");
      continue;  // started останется false, ниже это будет ошибкой
    }
    jobs[i].started = true;
  }

  for (unsigned int i = 0; i < jobs_num; i++) {
    if (jobs[i].started) pthread_join(threads[i], NULL);
  }

  // Каждый поток писал только в свой jobs[i], поэтому мьютекс не нужен.
  // Частичные произведения объединяем по модулю.
  uint64_t answer = 1 % mod;
  bool all_ok = true;
  for (unsigned int i = 0; i < jobs_num; i++) {
    if (!jobs[i].ok) {
      all_ok = false;
      continue;
    }
    answer = MultModulo(answer, jobs[i].answer, mod);
  }

  free(jobs);
  free(threads);
  free(to);

  if (!all_ok) {
    fprintf(stderr, "Some servers failed, the answer can not be computed\n");
    return 1;
  }

  printf("answer: %" PRIu64 "\n", answer);
  return 0;
}