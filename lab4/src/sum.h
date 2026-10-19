#ifndef SUM_H
#define SUM_H

#include <stdint.h>

struct SumArgs {
  int *array;
  uint32_t begin;  // включительно
  uint32_t end;    // не включительно
};

// Сумма элементов array[begin..end). Результат 64-битный, чтобы сумма
// большого массива случайных int не переполнялась.
uint64_t Sum(const struct SumArgs *args);

#endif