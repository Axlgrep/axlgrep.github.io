#include "../csapp.h"
#define MAXARGS 128

int parseline(char *buf, char **argv) {
  char *delim;       /* Points to first space delimiter */
  int argc;          /* number of args  */
  int bg;            /* Background job? */

  buf[strlen(buf) - 1] = ' ';     /*Replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) {
    buf++;
  }

  /* Build the argv list */
  argc = 0;
  while ((delim = strchr(buf, ' '))) {
    argv[argc++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) {
      buf++;
    }
  }
  argv[argc] = NULL;

  if (argc == 0) {  /* Ignore blank line */
    return 1;
  }

  /* Should the job run in the background */
  if ((bg = (*argv[argc - 1] == '&')) != 0) {
    argv[--argc] = NULL;
  }
  return bg;
}

/* If first arg is a builtin command, run it and return true */
int builtin_command(char **argv) {
  if (!strcmp(argv[0], "quit")) {  /* quit command */
    exit(0);
  }
  if (!strcmp(argv[0], "&")) {     /* ignore singleton & */
    return 1;
  }
  return 0;
}

void eval(char *cmdline) {
  char *argv[MAXARGS]; /* Argument list execve() */
  char buf[MAXLINE];   /* Holds modified command line */
  int bg;              /* Should the job run in bg or fg? */
  pid_t pid;           /* Process id */

  strcpy(buf, cmdline);
  bg = parseline(buf, argv);

  if (argv[0] == NULL) {
    return;   /* Ignore empty lines */
  }

  if (!builtin_command(argv)) {
    if ((pid = fork()) == 0) {    /* Child runs user job */
      if (execve(argv[0], argv, environ) < 0) {
        sleep(1);
        exit(0);
      }
    }
  }

  /* Parent waits for foreground job to terminate */
  if (!bg) {
    int status;
    if (waitpid(pid, &status, 0) < 0) {
      unix_error("waitfg: waitpid error");
    }
  } else {
    printf("%d %s", pid, cmdline);
  }
  return;
}

int main() {
  char cmdline[MAXLINE];

  while (1) {
    printf("> ");
    fgets(cmdline, MAXLINE, stdin);
    if (feof(stdin)) {
      exit(0);
    }
    eval(cmdline);
  }
  return 0;
}
