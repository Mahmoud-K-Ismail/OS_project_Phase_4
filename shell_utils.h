// header guard to prevent multiple inclusions of this file
#ifndef SHELL_UTILS_H
#define SHELL_UTILS_H

// need to include all -- need to use malloc, fork, exec, pipe, dup2, wait etc 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

// declaring these values as constants -- max size for various components
#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64
#define MAX_PIPES 10
#define MAX_FILENAME 256

// global constants
extern const char* SHELL_PROMPT;
extern const char* EXIT_COMMAND;

// enum for different error types --> better error handling
typedef enum {
    ERROR_NONE,
    ERROR_MISSING_FILE,
    ERROR_FILE_NOT_FOUND,
    ERROR_PERMISSION_DENIED,
    ERROR_INVALID_COMMAND,
    ERROR_FORK_FAILED,
    ERROR_EXEC_FAILED,
    ERROR_PIPE_FAILED,
    ERROR_DUP2_FAILED,
    ERROR_MALLOC_FAILED
} error_type_t;

// command structure for parsing
typedef struct {
    char** argv;              // for command and arguments
    int argc;                 // number of arguments
    char* input_file;         // input redirection file
    char* output_file;       // output redirection file
    char* error_file;         // error redirection file
    int has_input_redir;      // flag -- for input redirection
    int has_output_redir;     // flag -- for output redirection
    int has_error_redir;      // flag -- for error redirection

    // pipe related
    int is_piped;             // flag -- to indicate whether this command is part of a pipe
    int pipe_position;        // for position in pipe chain -- using 0 = first , -1 = last
} command_t;

// pipeline structure for handling multiple piped commands
// manages complete pipeline of commands connected by pipes
typedef struct {
    command_t** commands;     // array of commands in the pipeline
    int num_commands;         // number of commands in pipeline
    char* input_file;         // initial input redirection (for first command)
    char* output_file;        // final output redirection (for last command)
    char* error_file;         // error redirection
} pipeline_t;


// function prototypes for basic shell operations


// parses input string into tokens (command and arguments) -- returns Array of strings (argv), NULL-terminated
char** parse_input(char* input);

// parses a complete command line including redirections -- returns Pointer to command_t structure, NULL on error
command_t* parse_command_line(char* input);

// executes a simple command without pipes -- return 0 on success, error code on failure
int execute_simple_command(command_t* cmd);

// sets up file redirections using dup2() -- returns 0 on success, error code on failure
int setup_redirection(command_t* cmd);

// frees memory allocated for argv array
void free_argv(char** argv);

// frees memory allocated for command_t structure
void free_command(command_t* cmd);

// handles and displays error messages
void handle_error(error_type_t error, const char* context);

// trims whitespace from beginning and end of string, retunrs Pointer to trimmed string
char* trim_whitespace(char* str);

// checks if a string is empty or contains only whitespace -- returns1 if empty/whitespace only, 0 otherwise
int is_empty_string(const char* str);

// validates file access for redirection
int validate_file_access(const char* filename, int mode);


// parses input containing pipes and creates a pipeline structure
pipeline_t* parse_pipeline(char* input);

// executes a pipeline of commands connected by pipes
int execute_pipeline(pipeline_t* pipeline);

// frees memory allocated for pipeline_t structure
void free_pipeline(pipeline_t* pipeline);

// counts the number of pipes in input string -- returns number of pipe characters found
int count_pipes(const char* input);

// splits input string by pipe characters
char** split_by_pipes(char* input, int* num_segments);

// checks if a command is a built-in and executes it
int is_builtin_command(command_t* cmd);

// executes built-in echo command
int builtin_echo(command_t* cmd);

#endif /* SHELL_UTILS_H */
