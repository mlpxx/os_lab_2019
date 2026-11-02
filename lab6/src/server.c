#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <getopt.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "common.h"

#define MAX_THREADS 1024

struct FactorialArgs {
  uint64_t begin;
  uint64_t end;
  uint64_t mod;
  uint64_t result;  // сюда поток кладёт свой ответ
};

// Произведение чисел begin..end (включительно) по модулю mod.
// Если begin > end, диапазон пуст и результат равен 1 % mod.
uint64_t Factorial(const struct FactorialArgs *args) {
  uint64_t ans = 1 % args->mod;
  if (args->begin > args->end) return ans;

  // Цикл без условия i <= end: при end == UINT64_MAX счётчик бы не кончился.
  for (uint64_t i = args->begin;; i++) {
    ans = MultModulo(ans, i, args->mod);
    if (i == args->end) break;
  }

  return ans;
}

void *ThreadFactorial(void *args) {
  struct FactorialArgs *fargs = (struct FactorialArgs *)args;
  fargs->result = Factorial(fargs);
  return NULL;
}

// Делит числа begin..end между потоками, считает и объединяет результаты.
// Возвращает false, если не удалось создать поток.
static bool ParallelFactorial(uint64_t begin, uint64_t end, uint64_t mod,
                              uint64_t tnum, uint64_t *total) {
  uint64_t count = end >= begin ? end - begin + 1 : 0;

  // Потоков не больше, чем чисел: лишние получили бы пустой диапазон.
  uint64_t n = tnum;
  if (count < n) n = count > 0 ? count : 1;

  pthread_t threads[MAX_THREADS];
  struct FactorialArgs args[MAX_THREADS];

  // Первые extra потоков берут на одно число больше, остальные поровну.
  uint64_t chunk = count / n;
  uint64_t extra = count % n;
  uint64_t cur = begin;
  uint64_t created = 0;
  bool ok = true;

  for (uint64_t i = 0; i < n; i++) {
    uint64_t len = chunk + (i < extra ? 1 : 0);
    args[i].begin = cur;
    args[i].end = cur + len - 1;  // при len == 0 диапазон пуст (begin > end)
    args[i].mod = mod;
    args[i].result = 1 % mod;
    cur += len;

    if (pthread_create(&threads[i], NULL, ThreadFactorial, &args[i]) != 0) {
      printf("Error: pthread_create failed!\n");
      ok = false;
      break;
    }
    created++;
  }

  uint64_t result = 1 % mod;
  for (uint64_t i = 0; i < created; i++) {
    pthread_join(threads[i], NULL);
    result = MultModulo(result, args[i].result, mod);
  }

  *total = result;
  return ok;
}

int main(int argc, char **argv) {
  uint64_t tnum = 0;
  uint64_t port = 0;
  bool have_tnum = false;
  bool have_port = false;

  while (true) {
    static struct option options[] = {{"port", required_argument, 0, 0},
                                      {"tnum", required_argument, 0, 0},
                                      {0, 0, 0, 0}};

    int option_index = 0;
    int c = getopt_long(argc, argv, "", options, &option_index);

    if (c == -1)
      break;

    switch (c) {
    case 0: {
      switch (option_index) {
      case 0:
        if (!ConvertStringToUI64(optarg, &port) || port < 1 || port > 65535) {
          fprintf(stderr, "port must be a number from 1 to 65535\n");
          return 1;
        }
        have_port = true;
        break;
      case 1:
        if (!ConvertStringToUI64(optarg, &tnum) || tnum < 1 ||
            tnum > MAX_THREADS) {
          fprintf(stderr, "tnum must be a number from 1 to %d\n", MAX_THREADS);
          return 1;
        }
        have_tnum = true;
        break;
      default:
        printf("Index %d is out of options\n", option_index);
      }
    } break;

    case '?':
      printf("Unknown argument\n");
      return 1;
    default:
      fprintf(stderr, "getopt returned character code 0%o?\n", c);
    }
  }

  if (!have_port || !have_tnum) {
    fprintf(stderr, "Using: %s --port 20001 --tnum 4\n", argv[0]);
    return 1;
  }

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    fprintf(stderr, "Can not create server socket!\n");
    return 1;
  }

  struct sockaddr_in server;
  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_port = htons((uint16_t)port);
  server.sin_addr.s_addr = htonl(INADDR_ANY);

  int opt_val = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));

  int err = bind(server_fd, (struct sockaddr *)&server, sizeof(server));
  if (err < 0) {
    fprintf(stderr, "Can not bind to socket!\n");
    return 1;
  }

  err = listen(server_fd, 128);
  if (err < 0) {
    fprintf(stderr, "Could not listen on socket\n");
    return 1;
  }

  printf("Server listening at %d\n", (int)port);
  fflush(stdout);

  while (true) {
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);
    int client_fd = accept(server_fd, (struct sockaddr *)&client, &client_len);

    if (client_fd < 0) {
      fprintf(stderr, "Could not establish new connection\n");
      continue;
    }

    while (true) {
      // Запрос клиента: три числа uint64_t - begin, end, mod.
      char from_client[sizeof(uint64_t) * 3];
      int status = RecvAll(client_fd, from_client, sizeof(from_client));

      if (status == 0)  // клиент закрыл соединение между запросами
        break;
      if (status < 0) {
        fprintf(stderr, "Client read failed or sent wrong data format\n");
        break;
      }

      uint64_t begin = 0;
      uint64_t end = 0;
      uint64_t mod = 0;
      memcpy(&begin, from_client, sizeof(uint64_t));
      memcpy(&end, from_client + sizeof(uint64_t), sizeof(uint64_t));
      memcpy(&mod, from_client + 2 * sizeof(uint64_t), sizeof(uint64_t));

      fprintf(stdout, "Receive: %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", begin,
              end, mod);
      fflush(stdout);

      // Деление на ноль убило бы сервер, поэтому мусор отсекаем заранее.
      if (mod == 0 || begin == 0) {
        fprintf(stderr, "Client sent an invalid task\n");
        break;
      }

      uint64_t total = 0;
      if (!ParallelFactorial(begin, end, mod, tnum, &total)) {
        fprintf(stderr, "Can't compute the task\n");
        break;
      }

      printf("Total: %" PRIu64 "\n", total);
      fflush(stdout);

      if (!SendAll(client_fd, &total, sizeof(total))) {
        fprintf(stderr, "Can't send data to client\n");
        break;
      }
    }

    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
  }

  return 0;
}