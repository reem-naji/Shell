# MyShell

MyShell is a lightweight custom shell implementation written in C. It utilizes the [linenoise](https://github.com/antirez/linenoise) library to provide a modern command-line experience with line editing and persistent history. The shell follows the standard **REPL** (Read-Eval-Print Loop) cycle to process user input and execute commands.

## Features

- **Built-in Commands** — Native support for essential shell commands:
  - `cd` — Change the current working directory (supports `~` expansion).
  - `pwd` — Print the current working directory.
  - `history` — View the command history.
  - `exit` — Terminate the shell session.

- **External Command Execution** — Runs system programs by forking child processes and invoking `execvp()`.

- **Piping** — Connect the output of one command to the input of another using the `|` operator (e.g., `ls | grep .c`).

- **I/O Redirection** — Supports standard redirection operators:
  - `<` — Redirect input from a file.
  - `>` — Redirect output to a file (truncate).
  - `>>` — Redirect output to a file (append).

- **Background Execution** — Append `&` to run a command asynchronously without blocking the shell.

- **Signal Handling** — Intercepts `SIGINT` (Ctrl+C) to prevent the shell from terminating; child processes retain default signal behavior.

- **Dynamic Prompt** — Displays the current working directory with `~` shortening for the home path (e.g., `🪰 ~/projects $`).

- **Command History** — Maintains up to 1,024 entries, navigable with the arrow keys.

## Usage Examples

```bash
# Run an external command
🪰 ~ $ ls -la

# Use piping to filter results
🪰 ~ $ ls | grep .c

# Redirect output to a file (truncate)
🪰 ~ $ echo "hello" > output.txt

# Append output to a file
🪰 ~ $ echo "world" >> output.txt

# Redirect input from a file
🪰 ~ $ sort < input.txt

# Pipe combined with redirection
🪰 ~ $ cat myShell.c | head -n 20 > preview.txt

# Run a command in the background
🪰 ~ $ sleep 10 &
```

## Implementation Highlights: Piping Logic

The shell implements piping by splitting the command at the `|` token and creating a unidirectional data channel using the `pipe()` system call.

```c
// Simplified logic from s_execute_pipeline()
int fd[2];
pipe(fd);

if (fork() == 0) {
    dup2(fd[1], STDOUT_FILENO); // Child 1: stdout -> pipe write
    close(fd[0]); close(fd[1]);
    execvp(cmd1[0], cmd1);
}

if (fork() == 0) {
    dup2(fd[0], STDIN_FILENO);  // Child 2: stdin -> pipe read
    close(fd[0]); close(fd[1]);
    execvp(cmd2[0], cmd2);
}
```

## Testing Checklist

Use the following commands to verify the shell's core functionality:

| Feature | Test Command | Expected Result |
| :--- | :--- | :--- |
| **Piping** | `ls \| wc -l` | A number representing the total file count. |
| **Truncate** | `echo hello > a.txt` | File `a.txt` is created with "hello". |
| **Append** | `echo hi >> a.txt` | File `a.txt` now contains two lines. |
| **Input Redir** | `cat < a.txt` | Prints the contents of `a.txt`. |
| **Background** | `sleep 5 &` | Immediate prompt return; prints background PIDs. |
| **Pipe + Redir** | `ls \| grep .c > list.txt` | `list.txt` contains only `.c` filenames. |
| **Built-ins** | `pwd` / `cd ~` | Correct path reporting and home navigation. |

## Project Structure

| File | Description |
|---|---|
| `myShell.c` | Core shell logic: command parsing, pipeline execution, built-in implementations, and I/O redirection. |
| `linenoise.c` / `linenoise.h` | Self-contained line editing library for the interactive prompt and history management. |
| `Makefile` | Build script to automate compilation. |
| `.gitignore` | Configured to ignore object files (`.o`) and the compiled binary. |

## Getting Started

### Prerequisites

- **GCC** — The GNU Compiler Collection.
- **Make** — The build automation tool.

### Compilation

Build the project by running the following command in the project directory:

```bash
make
```

### Running the Shell

```bash
./myShell
```

## Credits

This project uses the [linenoise](https://github.com/antirez/linenoise) library by Salvatore Sanfilippo and Pieter Noordhuis for interactive line editing.
