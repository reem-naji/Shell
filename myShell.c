// make ./myShell

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "linenoise.h"

#define PROMPT "🪰  "
#define HISTORY_LENGTH 1024 
#define MAX_ARGS 1024
#define TOKEN_SEP " \t"
#define PATH_MAX 4096

char CWD[PATH_MAX];


int s_read(char *input, char **args, int max_args){
  int i = 0;
  char *token = strtok(input, TOKEN_SEP);
  while(token != NULL && i < (max_args - 1)) {
    args[i++] = token;
    token = strtok(NULL, " \t");
  }
  args[i] = NULL;
  return i;
}

int s_execute(char *cmd, char **cmd_args) {
  fprintf(stdout, "Executing `%s`\n", cmd);

  int status;
  pid_t pid;

  pid = fork();
  if (pid < 0){
    fprintf(stderr, "Could not execute\n");
    return -1;
  }
  if (pid ==0){
    execvp(cmd, cmd_args);
  } else {
    // parent: wait for child
    if (waitpid(pid, &status, 0) != pid){
      fprintf(stderr, "Could not wait for my dear child\n");
      return -1;
    }
  }

  return status;
}

void refresh_cwd(void){
  if(getcwd(CWD, sizeof(CWD)) == NULL) {
    fprintf(stderr,"Error: Could not read working dir");
    exit(1);
  }
}

typedef enum Builtin {
  CD,
  PWD,
  EXIT,
  HISTORY,
  INVALID
} Builtin;

void builtin_impl_cd(char **args, size_t n_args){
  char *new_dir = *args;
  if(chdir(new_dir) != 0){
    fprintf(stderr, "Error: Could not change dir");
    exit(1);
  }
  refresh_cwd();

}
void builtin_impl_pwd(char **args, size_t n_args){
  fprintf(stdout, "%s\n", CWD);
}
void builtin_impl_exit(char **args, size_t n_args){

}
void builtin_impl_history(char **args, size_t n_args){

}

void (*BUILTIN_TABLE[]) (char **args, size_t n_args) = {
  [CD] = builtin_impl_cd,
  [PWD] = builtin_impl_pwd,
  [EXIT] = builtin_impl_exit,
  [HISTORY] = builtin_impl_history
};

Builtin builtin_code(char *cmd) {
  if (!strncmp(cmd, "cd", 2)){
    return CD;
  } else if (!strncmp(cmd, "pwd", 3)){
    return PWD;
  } else if (!strncmp(cmd, "exit", 4)){
    return EXIT; 
  } else if (!strncmp(cmd, "history", 7)){
    return HISTORY;
  } else {
    return INVALID;
  }
}

int is_builtin(char *cmd) {
  return builtin_code(cmd) != INVALID;
}

void s_execute_builtin(char *cmd, char **args, size_t n_args) {
  BUILTIN_TABLE[builtin_code(cmd)](args, n_args);
}


int main(void) {
  refresh_cwd();

  if (!linenoiseHistorySetMaxLen(HISTORY_LENGTH)) {
    fprintf(stderr, "Could not set linenoise history length");
    exit(1);
  }

  char *line;
  char *args[MAX_ARGS];
  while((line = linenoise(PROMPT)) != NULL){

    // Read Step
    int args_read = s_read(line, args, MAX_ARGS);

    //Skip empty line
    if(args_read == 0){
      linenoiseFree(line);
      continue;
    }

    // Evaluation Step
    char *cmd = args[0];
    char **cmd_args = args;

    if (is_builtin(cmd)){
      s_execute_builtin(cmd, (cmd_args+1), args_read-1);
    } else{
      s_execute(cmd, cmd_args);

    }

    linenoiseHistoryAdd(line);
    linenoiseFree(line);
  }
  return 0;
}
