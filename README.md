# MyShell 

![C](https://img.shields.io/badge/language-C-blue.svg)
![OS](https://img.shields.io/badge/os-Linux%20%7C%20Unix-orange.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)

MyShell is a custom Unix-style command-line interpreter developed as the final project for the Operating Systems course. It simulates the core functionality of bash/sh, implementing process control, inter-process communication (IPC) via pipes, I/O redirection, and robust signal handling directly via system calls.

We integrated the [linenoise](https://github.com/antirez/linenoise) library to provide a modern, interactive prompt featuring command-line editing and persistent up/down arrow history.

---

## 📑 Table of Contents
- [Features](#-features)
- [System Architecture](#-system-architecture)
- [Installation & Build](#-installation--build)
- [Usage & Examples](#-usage--examples)
- [Project Structure](#-project-structure)
- [Known Limitations](#-known-limitations)

---

## ✨ Features

### ⚙️ Core Process Execution
- **REPL Loop:** A continuous Read-Eval-Print Loop that tokenizes input and executes external binaries via the `fork()` and `execvp()` system calls.
- **Background Jobs:** Appending an ampersand (`&`) detaches the process. The shell immediately returns control to the user and displays the background Process ID (PID).

### 🛠️ Built-in Commands
Executed natively within the parent process to alter the shell's state:
- `cd <path>`: Changes the directory. Features full `~` expansion for the `$HOME` environment variable.
- `pwd`: Resolves and prints the absolute working directory.
- `history`: Outputs a numbered list of the session's commands.
- `exit`: Safely terminates the shell and cleans up memory allocations.

### 🔄 IPC and Redirection
- **Multi-Piping (`|`):** Supports chaining commands together. The standard output of one process is bridged to the standard input of the next using `pipe()` and `dup2()`.
- **I/O Redirection (`>`, `<`):** - `>` Overwrites a target file with standard output.
  - `<` Feeds the contents of a file into standard input.

### 🛡️ Signal Handling
- **SIGINT (Ctrl+C):** Handled securely via `sigaction`. It interrupts running foreground child processes without killing the main shell process.
- **SIGCHLD (Zombie Reaping):** An asynchronous handler cleans up background processes when they terminate, ensuring the OS process table isn't polluted with zombie processes.

---

## 🏗️ System Architecture

### The Execution Engine
When a command is entered, the shell follows a strict pipeline:
1. **Parser:** Uses `strtok` to slice the input string into a null-terminated array of arguments.
2. **Built-in Check:** Compares the command against an `enum` of internal functions. If matched, it bypasses the fork step.
3. **Pipe Detector:** Scans for `|`. If found, it creates an array of file descriptors and forks multiple times, dynamically wiring the outputs to the inputs.
4. **Execution:** Child processes check for redirection symbols (`>`, `<`), alter their file descriptors accordingly, and replace their image using `execvp()`.

### Why `sigaction` over `signal()`?
We opted to use the newer POSIX `sigaction` standard instead of the deprecated `signal()` function. By applying the `SA_RESTART` flag, we ensure that interrupted system calls automatically resume, preventing the shell's `read` loop from crashing when a signal is caught.

---

## 🚀 Installation & Build

### Prerequisites
You will need a Linux/Unix environment with `gcc` and `make` installed.

```bash
# Clone the repository
git clone https://github.com/reem-naji/Shell.git
cd Shell

# Compile the binary
make
