#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: %s seed arraysize\n", argv[0]);
    return 1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return 1;
  }

  if (pid == 0) {
    // дочерний процесс заменяет себя программой sequential_min_max
    execl("./sequential_min_max", "sequential_min_max", argv[1], argv[2], (char *)NULL);
    perror("execl");   // сюда попадём только если exec не удался
    exit(1);
  }

  int status;
  waitpid(pid, &status, 0);
  printf("Child exited with status %d\n", WEXITSTATUS(status));
  return 0;
}