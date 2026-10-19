#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

// Демонстрация зомби-процесса.
// Использование: ./zombie [секунд_держать_зомби]   (по умолчанию 15)
int main(int argc, char **argv) {
  int hold = 15;
  if (argc > 1) hold = atoi(argv[1]);
  if (hold < 1) hold = 1;

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return 1;
  }

  if (pid == 0) {
    // Ребёнок сразу завершается.
    printf("[child  %d] exiting right now\n", getpid());
    exit(0);
  }

  // Родитель НЕ вызывает wait() - ребёнок остаётся зомби.
  printf("[parent %d] created child %d and does NOT call wait()\n", getpid(),
         pid);
  sleep(1);  // дадим ребёнку завершиться

  char cmd[128];
  snprintf(cmd, sizeof(cmd), "ps -o pid,ppid,stat,cmd -p %d", pid);
  printf("\n[parent] child state (STAT = Z means zombie, <defunct>):\n");
  fflush(stdout);
  system(cmd);

  printf("\n[parent] holding the zombie for %d s "
         "(check it in another terminal: %s)\n",
         hold, cmd);
  fflush(stdout);
  sleep(hold);

  waitpid(pid, NULL, 0);  // родитель "забирает" код возврата - зомби исчезает
  printf("\n[parent] wait() called, child state now:\n");
  fflush(stdout);
  if (system(cmd) != 0) printf("(no such process - zombie is gone)\n");

  return 0;
}