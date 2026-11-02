#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

uint64_t MultModulo(uint64_t a, uint64_t b, uint64_t mod) {
  uint64_t result = 0;
  a = a % mod;
  while (b > 0) {
    if (b % 2 == 1) {
      // result = (result + a) % mod. Оба слагаемых меньше mod, поэтому
      // сравниваем с остатком mod - a, чтобы сумма не вышла за 64 бита.
      result = (result >= mod - a) ? result - (mod - a) : result + a;
    }
    // a = (2 * a) % mod, по той же схеме.
    a = (a >= mod - a) ? a - (mod - a) : a + a;
    b /= 2;
  }

  return result % mod;
}

bool ConvertStringToUI64(const char *str, uint64_t *val) {
  // Первым символом должна быть цифра: так отсекаются пустая строка,
  // пробелы и знаки (strtoull молча превращает "-5" в огромное число).
  if (str == NULL || !isdigit((unsigned char)str[0])) return false;

  char *end = NULL;
  errno = 0;
  unsigned long long i = strtoull(str, &end, 10);
  if (errno == ERANGE || end == str || *end != '\0') return false;

  *val = i;
  return true;
}

bool SendAll(int fd, const void *buf, size_t len) {
  const char *p = (const char *)buf;
  while (len > 0) {
    // MSG_NOSIGNAL: если собеседник закрыл соединение, получим ошибку,
    // а не сигнал SIGPIPE, который убил бы весь процесс.
    ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    p += n;
    len -= (size_t)n;
  }
  return true;
}

int RecvAll(int fd, void *buf, size_t len) {
  char *p = (char *)buf;
  size_t got = 0;
  while (got < len) {
    ssize_t n = recv(fd, p + got, len - got, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (n == 0) return got == 0 ? 0 : -1;
    got += (size_t)n;
  }
  return 1;
}