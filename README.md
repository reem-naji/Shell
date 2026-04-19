MyShell
MyShell is a lightweight, simple shell implementation written in C that utilizes the linenoise library to provide a modern command-line experience with history and line editing capabilities. It follows the standard REPL (Read-Eval-Print Loop) cycle to process user inputs and execute commands.

Features
Standard REPL Cycle: Continuously reads input, evaluates it, prints results, and loops back for the next command.

Built-in Commands: Includes native support for essential commands:

cd: Change the current working directory.

pwd: Print the current working directory.

history: View the command history.

exit: Terminate the shell session.

External Command Execution: Capable of running system programs by using fork() to create child processes and execvp() for execution.

Command History: Maintains a history of up to 1,024 commands, allowing you to navigate previous inputs using the arrow keys.

Interactive Interface: Features a distinct 🪰  prompt and interactive line editing.

Project Structure
myShell.c: The core logic of the shell, including command parsing, the execution engine, and built-in implementations.

linenoise.c & linenoise.h: A self-contained line editing library used for the interactive prompt and history management.

Makefile: A build script to automate the compilation of the project.

.gitignore: Configured to ignore object files (.o) and the compiled binary.

Getting Started
Prerequisites
GCC: The GNU Compiler Collection.

Make: The build automation tool.

Compilation
To build the project, simply run the make command in your terminal within the project directory:

Bash
make
This command uses the provided Makefile to compile myShell.c and linenoise.c into a final executable named myShell.

Running the Shell
Once compiled, you can start the shell with:

Bash
./myShell
Credits
This project uses the linenoise library by Salvatore Sanfilippo and Pieter Noordhuis for its interactive line editing features.
