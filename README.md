# MyShell

MyShell is a lightweight custom shell implementation written in C. It utilizes the [linenoise](https://github.com/antirez/linenoise) library to provide a modern command-line experience with line editing and persistent history. The shell follows the standard **REPL** (Read-Eval-Print Loop) cycle to process user input and execute commands.

## Features

- **Built-in Commands** — Native support for essential shell commands:
  - `cd` — Change the current working directory (supports `~` expansion).
  - `pwd` — Print the current working directory.
  - `history` — View the command history.
  - `exit` — Terminate the shell session.

- **External Command Execution** — Runs system programs by forking child processes and invoking `execvp()`.

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

# Redirect output to a file (truncate)
🪰 ~ $ echo "hello" > output.txt

# Append output to a file
🪰 ~ $ echo "world" >> output.txt

# Redirect input from a file
🪰 ~ $ sort < input.txt

# Combine input and output redirection
🪰 ~ $ sort < input.txt > sorted.txt

# Run a command in the background
🪰 ~ $ sleep 10 &

# Change directory with tilde expansion
🪰 ~ $ cd ~/Documents
```

## Project Structure

| File | Description |
|---|---|
| `myShell.c` | Core shell logic: command parsing, execution engine, built-in implementations, and I/O redirection. |
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

This compiles `myShell.c` and `linenoise.c` into an executable named `myShell`.

### Running the Shell

```bash
./myShell
```

## Credits

This project uses the [linenoise](https://github.com/antirez/linenoise) library by Salvatore Sanfilippo and Pieter Noordhuis for interactive line editing.
