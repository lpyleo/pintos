# Shell

## Add suport for cd and pwd

### 1.cd实现

对解析的cmdline参数分析，如果为“”或者“～”则使用`getenv`获取HOME路径作为目标路径，否则参数即位目标路径。调用`chdir`函数访问目标路径，若失败则输出相关内容，成功则通过`getcwd`获取当前路径并输出

```c++
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
```

### 2.pwd实现

通过`getcwd`获取当前路径并输出

```c++
int cmd_pwd(unused struct tokens *tokens) {
  char cur_dir[4096];
  if (getcwd(cur_dir, 4096) == NULL) {
    printf("Error printing current directory\n");
  }
  printf("%s\n", cur_dir);
  return 1;
}
```



## Program execution

通过`fork`函数开启一个新线程来执行程序，对cmdline解析后的参数进行分析，如果存在>或者<则进行重定向，否则就向`execv`指定指令和参数进行执行。

```c++
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
```

## Path resolution

对于需要环境变量的命令，在执行`run_program`时通过`run_program_thru_path`执行命令，函数内通过`getenv`获取PATH环境变量地址，通过遍历尝试是否存在此命令，存在则通过`execv`指定命令地址和参数运行。

```c++

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
```

## Input/Output Redirection

在`run_program`函数中对cmdline的参数遍历，查询是否存在>或者<符号，存在则进行重定向。

```c++
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
```

`redirect`函数中通过调用`dup2`实现重定向功能

```c++
int redirect(int old_fd, int new_fd) {
  if (old_fd == -1 || dup2(old_fd, new_fd) == -1 || close(old_fd) == -1) {
    return -1;
  }
  return 1;
}
```

## Signal Handling and Terminal Control

使用`signal`函数对信号量进行控制，在初始化Shell时忽略信号量

```c++
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
```

在使用`run_program`调用`fork`创建新的线程时，在新线程内打开信号量默认处理方式

```c++
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
    ···
    for (int i = 0; i < sizeof(ignore_signals) / sizeof(int); i++) {
      signal(ignore_signals[i], SIG_DFL);
    }
    ···
  } else {
    int no_hang = run_bg ? WNOHANG : 0;
    waitpid(pid, &status, WUNTRACED|no_hang);
    tcsetpgrp(shell_terminal, shell_pgid);
  }
  return status;
}

```

## Background processing

使用系统调用`wait`函数一般用在父进程中等待回收子进程的资源，而防止僵尸进程的产生。

```c++
int cmd_wait(unused struct tokens *tokens) {
  int status, pid;
  while ((pid = wait(&status))) {
    if (pid == -1) {
      break;
    }
  }
  return 1;
}
```



