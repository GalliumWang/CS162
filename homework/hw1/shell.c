#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <paths.h>
#include <dirent.h>

#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_quit(tok_t arg[]);
int cmd_help(tok_t arg[]);
int cmd_pwd(tok_t arg[]);
int cmd_cd(tok_t arg[]);
char* find_file_from_path(char* filename);
void exec_process(tok_t arg[]);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(tok_t args[]);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_quit, "quit", "quit the command shell"},
  {cmd_pwd, "pwd", "show the current working directory"},
  {cmd_cd, "cd", "changes the current working directory to a specified directory"}
};

/**
 * Prints a helpful description for the given command
 */
int cmd_help(tok_t arg[]) {
  for (int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++) {
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  }
  return 1;
}

/**
 * Quits this shell
 */
int cmd_quit(tok_t arg[]) {
  exit(0);
  return 1;
}

/**
 * Prints the current working directory
 */
int cmd_pwd(tok_t arg[]) {
  char* cwd = getcwd(NULL, 0);
  if (cwd != NULL) {
    printf("%s\n", cwd);
    return 0;
  }
  return 1;
}

/**
 * Changes the current working directory to a specified directory
 */
int cmd_cd(tok_t arg[]) {
  if (chdir(arg[0]) == -1) {
    printf("%s : %s\n", arg[0], strerror(errno));
    return 1;
  }
  return 0;
}

/**
 * Find execuatable file from PATH.
 */
char* find_file_from_path(char* filename) {
  char* path = getenv("PATH");
  char* ret = (char*) malloc(PATH_MAX + MAXLINE + 2);
  tok_t* path_tokens;
  struct dirent* ent;
  if (path != NULL) {
    path_tokens = get_toks(path);
    for (int i = 1; i < MAXTOKS && path_tokens[i]; ++i) {
      DIR* dir;
      if ((dir = opendir(path_tokens[i])) == NULL) continue;
      while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, filename) == 0) {
          strncpy(ret, path_tokens[i], PATH_MAX);
          strncat(ret, "/", 1);
          strncat(ret, filename, MAXLINE);
          return ret;
        }
      }
      closedir(dir);
    }
  }
  return NULL;
}

/**
 * Exec process
 */
void exec_process(tok_t arg[]) {
  char* eval = find_file_from_path(arg[0]);
  if (eval != NULL) {
    arg[0] = eval;
  }
}



/**
 * Looks up the built-in command, if it exists.
 */
int lookup(char cmd[]) {
  for (int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++) {
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  }
  return -1;
}

/**
 * Intialization procedures for this shell
 */
void init_shell() {
  /* Check if we are running interactively */
  shell_terminal = STDIN_FILENO;
  shell_is_interactive = isatty(shell_terminal);

  if(shell_is_interactive){
    /* Force the shell into foreground */
    while(tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int shell(int argc, char *argv[]) {
  char *input_bytes;
  tok_t *tokens;
  int line_num = 0;
  int fundex = -1;

  init_shell();

  if (shell_is_interactive)
    /* Please only print shell prompts when standard input is not a tty */
    fprintf(stdout, "%d: ", line_num);

  while ((input_bytes = freadln(stdin))) {
    tokens = get_toks(input_bytes);
    fundex = lookup(tokens[0]);
    if (fundex >= 0) {
      cmd_table[fundex].fun(&tokens[1]);
    } else {
      /* REPLACE this to run commands as programs. */
      pid_t pid = fork();
      if (pid == 0) {
        exec_process(tokens);
        if (execv(tokens[0], tokens) < 0) {
          fprintf(stderr, "%s : Command not found\n", tokens[0]);
          exit(0);
        }
      } else {
        int child_status;
        if (waitpid(pid, &child_status, 0) < 0) {
          fprintf(stderr, "waitpid error\n");
        }
      }
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);
  }

  return 0;
}
