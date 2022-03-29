#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"
#define MAX_SIZE 1024
/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);
int cmd_wait(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", "print the current working directory"},
  {cmd_cd, "cd", "change the current working directory"},
  {cmd_wait, "wait", "wait for all background processes to finish"}
};

int ignore_signals[] = {
  SIGINT, SIGQUIT, SIGTERM, SIGTSTP,
  SIGCONT, SIGTTIN, SIGTTOU
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
  exit(0);
}

int cmd_pwd(unused struct tokens *tokens) {
  char cur_dir[4096];
  if (getcwd(cur_dir, 4096) == NULL) {
    printf("Error printing current directory\n");
  }
  printf("%s\n", cur_dir);
  return 1;
}

int cmd_cd(struct tokens *tokens) {
  char *targ_dir = tokens_get_token(tokens, 1);
  if (targ_dir == NULL || strcmp(targ_dir, "~") == 0) {
    targ_dir = getenv("HOME");
    if (targ_dir == NULL) {
      printf("Error changing directory\n");
      return -1;
    }
  }
  int success = chdir(targ_dir);
  if (success == -1) {
    printf("Error changing directory\n");
  }
  char cwd[MAX_SIZE];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    }
  return success;
}

int cmd_wait(unused struct tokens *tokens) {
  int status, pid;
  while ((pid = wait(&status))) {
    if (pid == -1) {
      break;
    }
  }
  return 1;
}

int run_program_thru_path(char *prog, char *args[]) {
  char *PATH = getenv("PATH");
  if (PATH == NULL) {
    return -1;
  }
  char prog_path[4096];
  char *path_dir = strtok(PATH, ":");
  while (path_dir != NULL) {
    sprintf(prog_path, "%s/%s", path_dir, prog);
    if (access(prog_path, F_OK) != -1) {
      return execv(prog_path, args);
    }
    path_dir = strtok(NULL, ":");
  }
  return -1;
}

int redirect(int old_fd, int new_fd) {
  if (old_fd == -1 || dup2(old_fd, new_fd) == -1 || close(old_fd) == -1) {
    return -1;
  }
  return 1;
}

int run_program(struct tokens *tokens) {
  int length = tokens_get_length(tokens);
  if (length == 0) {
    // user pressed return
    return 0;
  }
  int run_bg = length > 1 && strcmp(tokens_get_token(tokens, length - 1), "&") == 0;
  int pid = fork();
  int status = 0;
  if (pid == 0) {
    // process tokens into args array for exec, and redirect stdin/stdout
    char *args[length + 1];
    int redirect_stdin = 0, redirect_stdout = 0, num_args = 0;
    for (int i = 0; i < length; i++) {
      char *token = tokens_get_token(tokens, i);
      if (redirect_stdin) {
        int fd = open(token, O_RDONLY);
        if (redirect(fd, STDIN_FILENO) == -1) {
          printf("Error with input %s\n", token);
          exit(-1);
        }
        redirect_stdin = 0;
      } else if (redirect_stdout) {
        int fd = creat(token, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
        if (redirect(fd, STDOUT_FILENO) == -1) {
          printf("Error with input %s\n", token);
          exit(-1);
        }
        redirect_stdout = 0;
      } else if (strcmp(token, "<") == 0) {
        redirect_stdin = 1;
      } else if (strcmp(token, ">") == 0) {
        redirect_stdout = 1;
      } else if (!(i == length - 1 && run_bg)) {
        args[num_args++] = token;
      }
    }
    args[num_args] = (char *) NULL;
    char *prog = args[0];

    // move process to own process group
    setpgid(0, 0);
    if (!run_bg) {
      // move to foreground if input doesn't end with "&"
      tcsetpgrp(shell_terminal, getpgrp());
    }

    // override ignored signal handlers from shell to default signal handlers
    for (int i = 0; i < sizeof(ignore_signals) / sizeof(int); i++) {
      signal(ignore_signals[i], SIG_DFL);
    }
    // execute new program in child process, searching thru path env var if needed
    if (execv(prog, args) == -1 && run_program_thru_path(prog, args) == -1) {
      printf("Error executing program %s\n", prog);
      exit(-1);
    }
  } else {
    int no_hang = run_bg ? WNOHANG : 0;
    waitpid(pid, &status, WUNTRACED|no_hang);
    tcsetpgrp(shell_terminal, shell_pgid);
  }
  return status;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
  //忽略信号（SIG_IGN）：忽略信号，即使没有意义，代码执行仍将继续。
  for (int i = 0; i < sizeof(ignore_signals) / sizeof(int); i++) {
    signal(ignore_signals[i], SIG_IGN);
  }
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
      run_program(tokens);
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}