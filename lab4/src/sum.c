#include "sum.h"

uint64_t Sum(const struct SumArgs *args) {
  uint64_t sum = 0;
  for (uint32_t i = args->begin; i < args->end; i++) {
    sum += (uint64_t)args->array[i];
  }
  return sum;
}