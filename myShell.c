// make ./myShell

#define _POSIX_C_SOURCE 200809L

#include <stdio.h> // printf, fprintf, perror, snprintf
#include <stdlib.h> // malloc, free, exit, getenv
#include <string.h> // strcmp, strtok, strdup, strlen, strerror, memmove
#include <unistd.h> // fork, execvp, chdir, getcwd, dup2, pipe, close, STDOUT_FILENO, STDIN_FILENO
#include <sys/wait.h> // waitpid, WNOHANG
#include <signal.h> // signal, sigaction, SIGINT, SIGCHLD, SIG_DFL, SA_RESTART
#include <errno.h> // errno, EAGAIN
#include <fcntl.h> // open, O_WRONLY, O_CREAT, O_TRUNC, O_RDONLY
#include "linenoise.h" // linenoise (interactive line editing library)

#define HISTORY_LENGTH 1024 // max number of history entries
#define MAX_ARGS 1024 // max number of arguments per command
#define TOKEN_SEP " \t" // characters used to split input (space and tab)
#define PATH_MAX 4096 // max length of a file path
#define PROMPT_MAX 8192 // max length of the prompt string

char CWD[PATH_MAX]; // stores the current working directory
char PROMPT[PROMPT_MAX]; // stores the prompt string shown to user
static char *HISTORY[HISTORY_LENGTH]; // array of pointers to saved command strings
static int HISTORY_COUNT = 0; // how many history entries we have

// --- SIGNALS ---

void sigint_handler(int sig) {
  (void)sig; // suppress "unused parameter" warning
  ssize_t ret = write(STDOUT_FILENO, "\n", 1); // print a newline so the prompt looks clean
  (void)ret; // suppress "unused result" warning
}

void sigchld_handler(int sig) {
  (void)sig;
  int saved_errno = errno; // save errno so we don't corrupt it
  while (waitpid(-1, NULL, WNOHANG) > 0); // reap all finished child processes
  errno = saved_errno; // restore errno
}

// --- UTILITIES ---

void refresh_cwd(void) {
  if (getcwd(CWD, sizeof(CWD)) == NULL) {
    perror("myShell: getcwd");
  }
}

void build_prompt(void) {
  snprintf(PROMPT, sizeof(PROMPT), "🪰  ");
}

int s_read(char *input, char **args, int max_args) {
  int i = 0;
  char *token = strtok(input, TOKEN_SEP); // get first token
  while (token != NULL && i < (max_args - 1)) {
    args[i++] = token; // store each token in args array
    token = strtok(NULL, TOKEN_SEP); // get next token
  }
  args[i] = NULL; // NULL-terminate the array (required by execvp)
  return i; // return number of arguments
}

void history_push(const char *line) {
  if (line == NULL || *line == '\0') return; // ignore empty lines
  // don't save duplicate of the last command
  if (HISTORY_COUNT > 0 && strcmp(HISTORY[HISTORY_COUNT - 1], line) == 0) {
    return;
  }
  // if history is full, remove the oldest entry
  if (HISTORY_COUNT == HISTORY_LENGTH) {
    free(HISTORY[0]); // free oldest string
    memmove(HISTORY, HISTORY + 1, sizeof(char *) * (HISTORY_LENGTH - 1)); // shift everything left
    HISTORY_COUNT--;
  }
  char *copy = strdup(line); // make a copy of the string (allocates new memory)
  if (copy == NULL) {
    perror("myShell: strdup");
    return;
  }
  HISTORY[HISTORY_COUNT++] = copy; // store the copy in history and increment count
}

void history_free(void) {
  for (int i = 0; i < HISTORY_COUNT; i++) free(HISTORY[i]);
  HISTORY_COUNT = 0;
}

// --- BUILT-INS ---

typedef enum Builtin { CD, PWD, EXIT, HISTORY_CMD, INVALID } Builtin;

static char *expand_tilde(const char *path) {
  if (path == NULL) return NULL;
  if (path[0] != '~') return strdup(path); // no tilde, return as-is

  const char *home = getenv("HOME"); // get HOME environment variable
  if (home == NULL || *home == '\0') {
    return strdup(path); // HOME not set, return as-is
  }

  /* ~ alone */
  if (path[1] == '\0') return strdup(home); // "~" alone -> home directory

  /* ~/rest */
  if (path[1] == '/') { // "~/something" -> home + "/something"
    size_t len = strlen(home) + strlen(path);
    char *out = malloc(len);
    if (out == NULL) return NULL;
    snprintf(out, len, "%s%s", home, path + 1); // path+1 skips the '~'
    return out;
  }

  /* ~user is not supported; fall back to raw string */
  return strdup(path); // ~user syntax not supported
}

void builtin_impl_cd(char **args, size_t n_args) {
  const char *target;
  char *expanded = NULL;

  if (n_args < 1) { // "cd" with no arguments
    target = getenv("HOME"); // go to home directory
    if (target == NULL || *target == '\0') {
      fprintf(stderr, "myShell: cd: HOME not set\n");
      return;
    }
  } else {
    expanded = expand_tilde(args[0]); // expand ~ if present
    if (expanded == NULL) {
      perror("myShell: cd");
      return;
    }
    target = expanded;
  }

  if (chdir(target) != 0) { // actually change directory
    fprintf(stderr, "myShell: cd: %s: %s\n", target, strerror(errno));
  }
  free(expanded);
  refresh_cwd(); // update the global CWD
}

void builtin_impl_pwd(char **args, size_t n_args) {
  (void)args; (void)n_args; // mark parameters as intentionally unused
  fprintf(stdout, "%s\n", CWD);
}

void builtin_impl_exit(char **args, size_t n_args) {
  (void)args; (void)n_args;
  history_free(); // free all history memory before exiting
  exit(0); // terminate the shell with success status
}

void builtin_impl_history(char **args, size_t n_args) {
  (void)args; (void)n_args;
  for (int i = 0; i < HISTORY_COUNT; i++) {
    fprintf(stdout, "%5d  %s\n", i + 1, HISTORY[i]);
  }
}

void (*BUILTIN_TABLE[])(char **args, size_t n_args) = {
  [CD] = builtin_impl_cd,
  [PWD] = builtin_impl_pwd,
  [EXIT] = builtin_impl_exit,
  [HISTORY_CMD] = builtin_impl_history,
};

Builtin builtin_code(char *cmd) {
  if (strcmp(cmd, "cd") == 0) return CD;
  if (strcmp(cmd, "pwd") == 0) return PWD;
  if (strcmp(cmd, "exit") == 0) return EXIT;
  if (strcmp(cmd, "history") == 0) return HISTORY_CMD;
  return INVALID;
}

// --- REDIRECTION & EXECUTION ---
int handle_redirection(char **args) {
  for (int i = 0; args[i] != NULL; i++) {
    int is_out = strcmp(args[i], ">") == 0;
    int is_in = strcmp(args[i], "<") == 0;
    if (!is_out && !is_in) continue; // skip normal arguments

    if (args[i + 1] == NULL) { // no filename after > or <
      fprintf(stderr, "myShell: syntax error near '%s'\n", args[i]);
      return -1;
    }

    int fd;
    if (is_out) {
      // Open file for writing (create if needed, truncate if exists)
      fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd < 0) { perror("myShell: open"); return -1; }
      // Replace stdout (fd 1) with our file
      if (dup2(fd, STDOUT_FILENO) < 0) { perror("myShell: dup2"); close(fd); return -1; }
    } else {
      // Open file for reading
      fd = open(args[i + 1], O_RDONLY);
      if (fd < 0) { perror("myShell: open"); return -1; }
      // Replace stdin (fd 0) with our file
      if (dup2(fd, STDIN_FILENO) < 0) { perror("myShell: dup2"); close(fd); return -1; }
    }
    close(fd); // close the extra fd (dup2 already copied it)
    args[i] = NULL; // remove ">" or "<" from args
    args[i + 1] = NULL; // remove the filename from args
    break; // only handle one redirection
  }
  return 0;
}

int s_execute(char **cmd_args, int background) {
  pid_t pid = fork();
  if (pid < 0) {
    perror("myShell: fork");
    return -1;
  }
  if (pid == 0) {
    // --- CHILD PROCESS ---
    signal(SIGINT, SIG_DFL); // reset Ctrl+C to default (so child can be killed)
    signal(SIGCHLD, SIG_DFL); // reset SIGCHLD to default
    if (handle_redirection(cmd_args) < 0) _exit(1); // set up any redirections
    execvp(cmd_args[0], cmd_args); // replace this process with the command
    // If execvp returns, it means the command was not found
    fprintf(stderr, "myShell: %s: %s\n", cmd_args[0], strerror(errno));
    _exit(127);
  }

  // --- PARENT PROCESS (the shell) ---
  if (background) {
    fprintf(stdout, "[bg] pid=%d\n", (int)pid); // print PID and don't wait
  } else {
    waitpid(pid, NULL, 0); // wait for child to finish
  }
  return 0;
}

// --- PIPING ---

int s_execute_pipeline(char **args, int args_read, int background) {
  // Step 1: Find the pipe symbol
  int pipe_idx = -1;
  for (int i = 0; i < args_read; i++) {
    if (args[i] && strcmp(args[i], "|") == 0) { pipe_idx = i; break; }
  }

  // Step 2: If no pipe, run as a normal command or built-in
  if (pipe_idx == -1) {
    Builtin code = builtin_code(args[0]);
    if (code != INVALID) {
      BUILTIN_TABLE[code](args + 1, args_read - 1); // run built-in
      return 0;
    }
    return s_execute(args, background); // run external command
  }

  // Step 3: Split args into two commands at the pipe symbol
  args[pipe_idx] = NULL; // terminate first command's args
  char **cmd1 = args; // first command (before |)
  char **cmd2 = &args[pipe_idx + 1]; // second command (after |)

  // Step 4: Create a pipe
  int fd[2];
  if (pipe(fd) < 0) {
    perror("myShell: pipe");
    return -1;
  }
// fd[0] = read end of pipe
// fd[1] = write end of pipe

// Step 5: Fork first child (runs cmd1)
  pid_t p1 = fork();
  if (p1 < 0) {
    perror("myShell: fork");
    close(fd[0]); close(fd[1]);
    return -1;
  }
  if (p1 == 0) {
    signal(SIGINT, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    dup2(fd[1], STDOUT_FILENO); // redirect stdout to pipe's write end
    close(fd[0]); close(fd[1]); // close both ends (dup2 already copied fd[1])
    if (handle_redirection(cmd1) < 0) _exit(1);
    execvp(cmd1[0], cmd1);
    fprintf(stderr, "myShell: %s: %s\n", cmd1[0], strerror(errno));
    _exit(127);
  }

  // Step 6: Fork second child (runs cmd2)
  pid_t p2 = fork();
  if (p2 < 0) {
    perror("myShell: fork");
    close(fd[0]); close(fd[1]);
    waitpid(p1, NULL, 0);
    return -1;
  }
  if (p2 == 0) {
    signal(SIGINT, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    dup2(fd[0], STDIN_FILENO); // redirect stdin to pipe's read end
    close(fd[0]); close(fd[1]); // close both ends (dup2 already copied fd[0])
    if (handle_redirection(cmd2) < 0) _exit(1);
    execvp(cmd2[0], cmd2);
    fprintf(stderr, "myShell: %s: %s\n", cmd2[0], strerror(errno));
    _exit(127);
  }

  // Step 7: Parent closes pipe and waits
  close(fd[0]); close(fd[1]); // parent doesn't use the pipe

  if (background) {
    fprintf(stdout, "[bg] pid=%d %d\n", (int)p1, (int)p2);
  } else {
    waitpid(p1, NULL, 0); // wait for both children
    waitpid(p2, NULL, 0);
  }
  return 0;
}

// --- MAIN ---

int main(void) {
  refresh_cwd(); // get initial working directory
  build_prompt(); // set up the prompt string

  struct sigaction sa_int = {0};
  sa_int.sa_handler = sigint_handler; // our custom handler
  sigemptyset(&sa_int.sa_mask); // don't block other signals during handle
  sa_int.sa_flags = SA_RESTART; // restart interrupted system calls
  if (sigaction(SIGINT, &sa_int, NULL) < 0) perror("myShell: sSSigaction SIGINT"); // install the handler

  struct sigaction sa_chld = {0};
  sa_chld.sa_handler = sigchld_handler;
  sigemptyset(&sa_chld.sa_mask);
  sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP; // only notify on child exit, not stop
  if (sigaction(SIGCHLD, &sa_chld, NULL) < 0) perror("myShell: sigaction SIGCHLD");

  linenoiseHistorySetMaxLen(HISTORY_LENGTH); // configure linenoise history size

  char *line;
  char *args[MAX_ARGS];
  while ((line = linenoise(PROMPT)) != NULL || errno == EAGAIN) {
    // linenoise() displays the prompt, reads input, returns the line
    // Returns NULL on EOF (Ctrl+D) -> exits the loop

    if (errno == EAGAIN) { errno = 0;  continue; }
    // EAGAIN means the read was interrupted by a signal -> skip and retry

    char *line_for_history = strdup(line);
    // Save a copy because s_read() modifies the original string (strtok)

    int args_read = s_read(line, args, MAX_ARGS);
    // Parse the line into args array, and get the count of arguments
    if (args_read == 0) {
      // Empty input (user just pressed Enter)
      free(line_for_history);
      linenoiseFree(line);
      continue;
    }

    // Check for background execution (if last argument is "&")
    int background = 0;
    if (args_read > 0 && strcmp(args[args_read - 1], "&") == 0) {
      background = 1;
      args[--args_read] = NULL; // remove "&" from args and decrease count
    }

    // Execute the command (handles built-ins, external commands, and pipes)
    if (args_read > 0) {
      s_execute_pipeline(args, args_read, background);
    }

    // Save to both linenoise history (arrow key navigation) and our history
    if (line_for_history != NULL) {
      linenoiseHistoryAdd(line_for_history); // linenoise's internal history
      history_push(line_for_history); // our history (for the history command)
      free(line_for_history);
    }
    linenoiseFree(line); // free the line allocated by linenoise
    build_prompt(); // rebuild prompt (in case CWD changed)
  }

  history_free(); // clean up on exit
  return 0;
}
