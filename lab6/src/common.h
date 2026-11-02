#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Код, который нужен и клиенту, и серверу (раньше он дублировался).

// (a * b) % mod без переполнения 64 бит (метод удвоения и сложения).
// mod должен быть больше нуля.
uint64_t MultModulo(uint64_t a, uint64_t b, uint64_t mod);

// Разбор беззнакового числа из строки. Возвращает false, если строка
// не число (в том числе пустая или с минусом) или не влезает в uint64_t.
bool ConvertStringToUI64(const char *str, uint64_t *val);

// Отправить ровно len байт: send может отправить меньше, чем просили.
bool SendAll(int fd, const void *buf, size_t len);

// Принять ровно len байт: recv может вернуть меньше, чем просили.
// Возвращает: 1 - всё принято, 0 - соединение закрыто до первого байта,
// -1 - ошибка или соединение закрыто посреди сообщения.
int RecvAll(int fd, void *buf, size_t len);

#endif