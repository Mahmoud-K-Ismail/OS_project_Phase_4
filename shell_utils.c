// new corrected version

// define posix feature test
#define _POSIX_C_SOURCE 200809L
#include "shell_utils.h"
#include <glob.h>

// trim leading and trailing ascii whitespace characters in place and return the start pointer
char* trim_whitespace(char* str) {
    // declare an end pointer used to walk from the tail
    char* end;
    // advance start pointer over leading spaces, tabs, newlines, and carriage returns
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    // if the string is now empty, return it as-is
    if (*str == '\0') return str;
    // position end at the last valid character
    end = str + strlen(str) - 1;
    // move end backward over trailing whitespace
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    // null-terminate one past the last non-whitespace character
    *(end + 1) = '\0';
    // return the trimmed start pointer
    return str;
}

// check whether a c-string is null or contains only ascii whitespace
int is_empty_string(const char* str) {
    if (str == NULL) return 1;
    // scan until a non-whitespace character is found
    while (*str) {
        if (*str != ' ' && *str != '\t' && *str != '\n' && *str != '\r') return 0;
        str++;
    }
    // all characters were whitespace
    return 1;
}

// helper function to process escape sequences in a string
char* process_escape_sequences(const char* str) {
    size_t len = strlen(str);
    char* result = malloc(len + 1);
    if (!result) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\\' && i + 1 < len) {
            switch (str[i + 1]) {
                case 'n': result[j++] = '\n'; i++; break;
                case 't': result[j++] = '\t'; i++; break;
                case 'r': result[j++] = '\r'; i++; break;
                case '\\': result[j++] = '\\'; i++; break;
                case '\'': result[j++] = '\''; i++; break;
                case '\"': result[j++] = '\"'; i++; break;
                default: result[j++] = str[i]; break;
            }
        } else {
            result[j++] = str[i];
        }
    }
    result[j] = '\0';
    return result;
}

// parse a command string into argv handling quotes, escape sequences, and wildcards
char** parse_input(char* input) {
    // allocate space for a fixed maximum number of arguments plus terminator
    char** argv = malloc(MAX_ARGS * sizeof(char*));
    // check allocation result
    if (!argv) { handle_error(ERROR_MALLOC_FAILED, "parse_input"); return NULL; }
    
    int argc = 0;
    char* ptr = input;
    
    while (*ptr && argc < MAX_ARGS - 1) {
        // skip leading whitespace
        while (*ptr == ' ' || *ptr == '\t' || *ptr == '\n') ptr++;
        if (*ptr == '\0') break;
        
        // build one argument which may consist of multiple quoted and unquoted parts
        char* arg_buffer = malloc(MAX_INPUT_SIZE);
        if (!arg_buffer) {
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(argv);
            handle_error(ERROR_MALLOC_FAILED, "parse_input arg_buffer");
            return NULL;
        }
        arg_buffer[0] = '\0';
        size_t arg_pos = 0;
        
        // keep building the argument until hit whitespace
        while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n') {
            // check if this part is quoted
            char quote_char = '\0';
            if (*ptr == '\'' || *ptr == '\"') {
                quote_char = *ptr;
                ptr++;
            }
            
            char* start = ptr;
            
            if (quote_char) {
                // find matching closing quote
                while (*ptr && *ptr != quote_char) ptr++;
                
                if (*ptr == quote_char) {
                    // extract the quoted content
                    size_t part_len = ptr - start;
                    char* temp = malloc(part_len + 1);
                    if (!temp) {
                        free(arg_buffer);
                        for (int i = 0; i < argc; i++) free(argv[i]);
                        free(argv);
                        handle_error(ERROR_MALLOC_FAILED, "parse_input temp");
                        return NULL;
                    }
                    strncpy(temp, start, part_len);
                    temp[part_len] = '\0';
                    
                    // process escape sequences
                    char* processed = process_escape_sequences(temp);
                    free(temp);
                    
                    if (!processed) {
                        free(arg_buffer);
                        for (int i = 0; i < argc; i++) free(argv[i]);
                        free(argv);
                        handle_error(ERROR_MALLOC_FAILED, "parse_input escape processing");
                        return NULL;
                    }
                    
                    // append to arg_buffer
                    strcpy(arg_buffer + arg_pos, processed);
                    arg_pos += strlen(processed);
                    free(processed);
                    
                    ptr++; // skip closing quote
                } else {
                    // unclosed quote - treat rest as part of the argument
                    strcpy(arg_buffer + arg_pos, start);
                    arg_pos += strlen(start);
                    break;
                }
            } else {
                // unquoted part - find next whitespace or quote
                while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n' && 
                       *ptr != '\'' && *ptr != '\"') ptr++;
                size_t part_len = ptr - start;
                
                // append to arg_buffer
                strncpy(arg_buffer + arg_pos, start, part_len);
                arg_pos += part_len;
                arg_buffer[arg_pos] = '\0';
            }
        }
        
        // have a complete argument in arg_buffer
        // check if it contains wildcards and needs glob expansion
        int has_wildcard = 0;
        for (size_t i = 0; i < arg_pos; i++) {
            if (arg_buffer[i] == '*' || arg_buffer[i] == '?' || arg_buffer[i] == '[') {
                has_wildcard = 1;
                break;
            }
        }
        
        if (has_wildcard) {
            // perform glob expansion
            glob_t glob_result;
            int glob_ret = glob(arg_buffer, GLOB_NOCHECK, NULL, &glob_result);
            
            if (glob_ret == 0) {
                // add all matched files as separate arguments
                for (size_t i = 0; i < glob_result.gl_pathc && argc < MAX_ARGS - 1; i++) {
                    argv[argc] = malloc(strlen(glob_result.gl_pathv[i]) + 1);
                    if (!argv[argc]) {
                        globfree(&glob_result);
                        free(arg_buffer);
                        for (int j = 0; j < argc; j++) free(argv[j]);
                        free(argv);
                        handle_error(ERROR_MALLOC_FAILED, "parse_input glob");
                        return NULL;
                    }
                    strcpy(argv[argc], glob_result.gl_pathv[i]);
                    argc++;
                }
                globfree(&glob_result);
            } else {
                // failed -- use literal string
                argv[argc] = malloc(strlen(arg_buffer) + 1);
                if (!argv[argc]) {
                    free(arg_buffer);
                    for (int i = 0; i < argc; i++) free(argv[i]);
                    free(argv);
                    handle_error(ERROR_MALLOC_FAILED, "parse_input literal");
                    return NULL;
                }
                strcpy(argv[argc], arg_buffer);
                argc++;
            }
        } else {
            // no wildcards -- just add the argument as-is
            argv[argc] = malloc(strlen(arg_buffer) + 1);
            if (!argv[argc]) {
                free(arg_buffer);
                for (int i = 0; i < argc; i++) free(argv[i]);
                free(argv);
                handle_error(ERROR_MALLOC_FAILED, "parse_input no wildcard");
                return NULL;
            }
            strcpy(argv[argc], arg_buffer);
            argc++;
        }
        
        free(arg_buffer);
    }
    
    argv[argc] = NULL;
    // return the constructed argv
    return argv;
}

// print a descriptive error message for a given error code and optional context string
void handle_error(error_type_t error, const char* context) {
    const char *ctx = context ? context : "(null)";
    switch (error) {
        case ERROR_MISSING_FILE: fprintf(stderr, "Error: Missing filename for redirection\n"); break;
        case ERROR_FILE_NOT_FOUND: fprintf(stderr, "Error: File '%s' not found or cannot be accessed\n", ctx); break;
        case ERROR_PERMISSION_DENIED:fprintf(stderr, "Error: Permission denied for file '%s'\n", ctx); break;
        case ERROR_INVALID_COMMAND: fprintf(stderr, "Error: Invalid command '%s'\n", ctx); break;
        case ERROR_FORK_FAILED: fprintf(stderr, "Error: Failed to create child process for '%s'\n", ctx); break;
        case ERROR_EXEC_FAILED: fprintf(stderr, "Error: Failed to execute command '%s'\n", ctx); break;
        case ERROR_PIPE_FAILED: fprintf(stderr, "Error: Failed to create pipe\n"); break;
        case ERROR_DUP2_FAILED: fprintf(stderr, "Error: Failed to set up redirection (%s)\n", ctx); break;
        case ERROR_MALLOC_FAILED: fprintf(stderr, "Error: Memory allocation failed (%s)\n", ctx); break;
        default: fprintf(stderr, "Error: Unknown error occurred\n"); break;
    }
}

// free a null-terminated argv vector and the vector itself
void free_argv(char** argv) {
    // ignore null pointer input
    if (!argv) return;
    // free each string slot until the null terminator
    for (int i = 0; argv[i] != NULL; i++) free(argv[i]);
    // free the container array
    free(argv);
}

// free the container array
void free_command(command_t* cmd) {
    if (!cmd) return;
    free_argv(cmd->argv);
    // free any redirection path strings if allocated
    if (cmd->input_file) free(cmd->input_file);
    if (cmd->output_file) free(cmd->output_file);
    if (cmd->error_file) free(cmd->error_file);
    free(cmd);
}

// free an entire pipeline_t including its commands and redirection paths
void free_pipeline(pipeline_t* pipeline) {
    if (!pipeline) return;
    if (pipeline->commands) {
        for (int i = 0; i < pipeline->num_commands; i++) free_command(pipeline->commands[i]);
        free(pipeline->commands);
    }
    // free pipeline-level redirection strings
    if (pipeline->input_file) free(pipeline->input_file);
    if (pipeline->output_file) free(pipeline->output_file);
    if (pipeline->error_file) free(pipeline->error_file);
    // free the pipeline container itself
    free(pipeline);
}

// provide a thin wrapper around access() for readability in callers
int validate_file_access(const char* filename, int mode) {
    return access(filename, mode) == 0;
}

// parse a single command string including optional redirections into a command_t
command_t* parse_command_line(char* input) {
    // allocate the command structure
    command_t* cmd = malloc(sizeof(command_t));
    if (!cmd) { handle_error(ERROR_MALLOC_FAILED, "parse_command_line"); return NULL; }
    // initialize pointers and flags to safe defaults
    cmd->argv = NULL; cmd->argc = 0;
    cmd->input_file = NULL; cmd->output_file = NULL; cmd->error_file = NULL;
    cmd->has_input_redir = 0; cmd->has_output_redir = 0; cmd->has_error_redir = 0;
    cmd->is_piped = 0; cmd->pipe_position = -1;
    
    // duplicate the input
    char* working_input = malloc(strlen(input) + 1);
    // check duplication success
    if (!working_input) { free(cmd); handle_error(ERROR_MALLOC_FAILED, "working input copy"); return NULL; }
    strcpy(working_input, input);
    
    // use a sliding pointer for searching
    char* current = working_input;
    
    // locate input redirection character '<' if present
    char* input_pos = strstr(current, "<");
    if (input_pos) {

        // record that input redirection was specified
        cmd->has_input_redir = 1;

        // compute the start of the filename after '<'
        char* filename_start = input_pos + 1;

        // skip any spaces or tabs after '<'
        while (*filename_start == ' ' || *filename_start == '\t') filename_start++;

        // if nothing follows --> syntax E
        if (*filename_start == '\0') { handle_error(ERROR_MISSING_FILE, "input redirection"); free(working_input); free_command(cmd); return NULL; }
        // find the end of the filename by stopping at whitespace or another redirection symbol
        char* filename_end = filename_start;

        while (*filename_end && *filename_end!=' ' && *filename_end!='\t' && *filename_end!='>' && *filename_end!='<') filename_end++;
        int filename_len = (int)(filename_end - filename_start);
        cmd->input_file = malloc(filename_len + 1);

        // verify allocation
        if (!cmd->input_file) { free(working_input); free_command(cmd); handle_error(ERROR_MALLOC_FAILED, "input filename"); return NULL; }
        // copy filename and null-terminate
        strncpy(cmd->input_file, filename_start, filename_len);
        cmd->input_file[filename_len] = '\0';

        // remove the redirection chunk from the working string by collapsing the tail over it
        memmove(input_pos, filename_end, strlen(filename_end) + 1);
    }
    
    // locate stderr redirection token "2>" if present
    char* error_pos = strstr(current, "2>");
    if (error_pos) {
        // record the presence of an error redirection
        cmd->has_error_redir = 1;
        
        // starting position is two characters past '2>'
        char* filename_start = error_pos + 2;
        
        // skip whitespace after '2>'
        while (*filename_start == ' ' || *filename_start == '\t') filename_start++;
        
        // ensure a filename is present
        if (*filename_start == '\0') { handle_error(ERROR_MISSING_FILE, "error redirection"); free(working_input); free_command(cmd); return NULL; }
        
        // find end of the filename span
        char* filename_end = filename_start;
        while (*filename_end && *filename_end!=' ' && *filename_end!='\t' && *filename_end!='>' && *filename_end!='<') filename_end++;
        
        // compute filename length
        int filename_len = (int)(filename_end - filename_start);
        
        // allocate storage for stderr filename
        cmd->error_file = malloc(filename_len + 1);
        
        // verify allocation
        if (!cmd->error_file) { free(working_input); free_command(cmd); handle_error(ERROR_MALLOC_FAILED, "error filename"); return NULL; }
        
        // copy filename and terminate
        strncpy(cmd->error_file, filename_start, filename_len);
        cmd->error_file[filename_len] = '\0';
        
        // remove the "2> filename" portion from the working input
        memmove(error_pos, filename_end, strlen(filename_end) + 1);
    }
    
    // locate stdout redirection '>' -- safe now that "2>" has been removed if present
    char* output_pos = strstr(current, ">");
    if (output_pos) {
        // record presence of output redirection
        cmd->has_output_redir = 1;
        // compute start of the filename after '>'
        char* filename_start = output_pos + 1;
        // skip spaces or tabs
        while (*filename_start == ' ' || *filename_start == '\t') filename_start++;
        // ensure a filename is present
        if (*filename_start == '\0') { handle_error(ERROR_MISSING_FILE, "output redirection"); free(working_input); free_command(cmd); return NULL; }
        // find end of filename
        char* filename_end = filename_start;
        while (*filename_end && *filename_end!=' ' && *filename_end!='\t' && *filename_end!='>' && *filename_end!='<') filename_end++;
        // compute filename length
        int filename_len = (int)(filename_end - filename_start);
        // allocate storage for stdout filename
        cmd->output_file = malloc(filename_len + 1);
        // verify allocation
        if (!cmd->output_file) { free(working_input); free_command(cmd); handle_error(ERROR_MALLOC_FAILED, "output filename"); return NULL; }
        // copy filename and null-terminate
        strncpy(cmd->output_file, filename_start, filename_len);
        cmd->output_file[filename_len] = '\0';
        // remove the "> filename" portion from the working input
        memmove(output_pos, filename_end, strlen(filename_end) + 1);
    }
    
    // trim the remaining command text after removing redirections
    char* command_part = trim_whitespace(working_input);
    // if nothing remains, the command is invalid
    if (strlen(command_part) == 0) { handle_error(ERROR_INVALID_COMMAND, "empty command"); free(working_input); free_command(cmd); return NULL; }
    
    // parse arguments into argv
    cmd->argv = parse_input(command_part);
    // validate argv creation
    if (!cmd->argv) { free(working_input); free_command(cmd); return NULL; }
    
    // count the arguments in argv until the null terminator
    cmd->argc = 0; while (cmd->argv[cmd->argc] != NULL) cmd->argc++;
    
    // free temporary working buffer
    free(working_input);
    // return the parsed command structure
    return cmd;
}

// execute built-in echo command with -e flag support
int builtin_echo(command_t* cmd) {
    int interpret_escapes = 0;
    int start_idx = 1; // start after "echo"
    
    // check if -e flag is present
    if (cmd->argc > 1 && strcmp(cmd->argv[1], "-e") == 0) {
        interpret_escapes = 1;
        start_idx = 2; // skip "echo" and "-e"
    }
    
    // print arguments
    for (int i = start_idx; i < cmd->argc; i++) {
        if (i > start_idx) {
            printf(" "); // space between arguments
        }
        
        if (interpret_escapes) {
            // process and print with escape sequences
            char* str = cmd->argv[i];
            while (*str) {
                if (*str == '\\' && *(str + 1)) {
                    str++;
                    switch (*str) {
                        case 'n': printf("\n"); break;
                        case 't': printf("\t"); break;
                        case 'r': printf("\r"); break;
                        case '\\': printf("\\"); break;
                        case 'a': printf("\a"); break;
                        case 'b': printf("\b"); break;
                        case 'c': return 0; // stop printing
                        case 'e': printf("\033"); break;
                        case 'f': printf("\f"); break;
                        case 'v': printf("\v"); break;
                        default: printf("\\%c", *str); break;
                    }
                } else {
                    printf("%c", *str);
                }
                str++;
            }
        } else {
            // print literally
            printf("%s", cmd->argv[i]);
        }
    }
    
    printf("\n"); // echo always adds newline at end
    return 0;
}

// check if command is a built-in and execute it
int is_builtin_command(command_t* cmd) {
    if (!cmd || !cmd->argv || !cmd->argv[0]) return 0;
    
    if (strcmp(cmd->argv[0], "echo") == 0) {
        return 1;
    }
    
    return 0;
}

// apply any redirections in a command by wiring file descriptors via dup2()
int setup_redirection(command_t* cmd) {
    int fd;
    if (cmd->has_input_redir) {
        // verify readability using access() for a clearer error
        if (access(cmd->input_file, R_OK) != 0) { handle_error(ERROR_FILE_NOT_FOUND, cmd->input_file); return -1; }
        fd = open(cmd->input_file, O_RDONLY);
        if (fd == -1) { handle_error(ERROR_FILE_NOT_FOUND, cmd->input_file); return -1; }
        if (dup2(fd, STDIN_FILENO) == -1) { handle_error(ERROR_DUP2_FAILED, "input redirection"); close(fd); return -1; }
        close(fd);
    }
    if (cmd->has_output_redir) {
        // open the output file with create and truncate semantics and user-writable mode
        fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) { handle_error(ERROR_PERMISSION_DENIED, cmd->output_file); return -1; }
        if (dup2(fd, STDOUT_FILENO) == -1) { handle_error(ERROR_DUP2_FAILED, "output redirection"); close(fd); return -1; }
        close(fd);
    }
    // handle stderr redirection if requested
    if (cmd->has_error_redir) {
        fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) { handle_error(ERROR_PERMISSION_DENIED, cmd->error_file); return -1; }
        if (dup2(fd, STDERR_FILENO) == -1) { handle_error(ERROR_DUP2_FAILED, "error redirection"); close(fd); return -1; }
        close(fd);
    }
    return 0;
}

// execute a single command by forking and calling execvp -- wait for completion and return exit status
int execute_simple_command(command_t* cmd) {
    // check if this is a built-in command
    if (is_builtin_command(cmd)) {
        // for built-ins, we need to handle redirections in the parent process
        // save original stdin/stdout/stderr
        int saved_stdin = -1, saved_stdout = -1, saved_stderr = -1;
        
        if (cmd->has_input_redir || cmd->has_output_redir || cmd->has_error_redir) {
            saved_stdin = dup(STDIN_FILENO);
            saved_stdout = dup(STDOUT_FILENO);
            saved_stderr = dup(STDERR_FILENO);
            
            // set up redirections
            if (setup_redirection(cmd) != 0) {
                if (saved_stdin != -1) close(saved_stdin);
                if (saved_stdout != -1) close(saved_stdout);
                if (saved_stderr != -1) close(saved_stderr);
                return -1;
            }
        }
        
        // execute the built-in
        int result = 0;
        if (strcmp(cmd->argv[0], "echo") == 0) {
            result = builtin_echo(cmd);
        }
        
        // restore original file descriptors
        if (saved_stdin != -1) {
            dup2(saved_stdin, STDIN_FILENO);
            close(saved_stdin);
        }
        if (saved_stdout != -1) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
        if (saved_stderr != -1) {
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stderr);
        }
        
        return result;
    }
    
    // fork child process for external commands
    pid_t pid = fork();
    // declare status container for wait()
    int status;
    
    // handle fork failure
    if (pid == -1) {
        // report fork failure with command name if available
        handle_error(ERROR_FORK_FAILED, cmd->argv ? cmd->argv[0] : "(null)");
        return -1;
    // child process branch
    } else if (pid == 0) {
        // set up any requested redirections in the child
        if (setup_redirection(cmd) != 0) exit(EXIT_FAILURE);
        // attempt to replace process image with the requested program
        if (execvp(cmd->argv[0], cmd->argv) == -1) {
            // provide user-friendly messages for not found and permission errors
            if (errno == ENOENT) fprintf(stderr, "Command not found: %s\n", cmd->argv[0]);
            else if (errno == EACCES) fprintf(stderr, "Permission denied: %s\n", cmd->argv[0]);
            else handle_error(ERROR_EXEC_FAILED, cmd->argv[0]);
            // terminate child with failure code
            exit(EXIT_FAILURE);
        }
    // parent process branch
    } else {
        // wait for the child to finish
        if (wait(&status) == -1) { handle_error(ERROR_INVALID_COMMAND, "wait failed"); return -1; }
        // if the child exited normally, return its exit status
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        return -1;
    }
    return 0;
}

// count the number of pipe characters in an input string
int count_pipes(const char* input) {
    int count = 0;
    const char* ptr = input;
    while (*ptr) {
        if (*ptr == '|') count++;
        ptr++;
    }
    return count;
}

// split a pipeline string into command segments
char** split_by_pipes(char* input, int* num_segments) {
    char* trimmed_input = trim_whitespace(input);
    size_t len = strlen(trimmed_input);
    // reject a leading pipe which indicates a missing command before it
    if (len > 0 && trimmed_input[0] == '|') { fprintf(stderr, "Error: Missing command before pipe\n"); return NULL; }
    // reject a trailing pipe which indicates a missing command after it
    if (len > 0 && trimmed_input[len - 1] == '|') { fprintf(stderr, "Error: Command missing after pipe\n"); return NULL; }
    
    // scan once to detect empty segments like "cmd1 | | cmd3"
    const char *p = trimmed_input;
    // flag to remember being immediately after a pipe
    int after_pipe = 0;
    while (*p) {
        if (*p == '|') {
            // if we were already after a pipe and only whitespace seen --> an empty segment
            if (after_pipe) { fprintf(stderr, "Error: Empty command between pipes\n"); return NULL; }
            after_pipe = 1;
        } else if (*p != ' ' && *p != '\t') {
            after_pipe = 0;
        }
        ++p;
    }
    
    // compute number of segments as pipes+1
    int capacity = count_pipes(trimmed_input) + 1;
    // if there is only one segment, treat as invalid for the splitter
    if (capacity < 2) { fprintf(stderr, "Error: Invalid pipeline\n"); return NULL; }
    char** segments = malloc(capacity * sizeof(char*));
    // validate allocation
    if (!segments) { handle_error(ERROR_MALLOC_FAILED, "split_by_pipes"); return NULL; }
    
    // initialize a count of segments filled
    int seg_count = 0;
    // start points at beginning of current segment
    const char* start = trimmed_input;
    // current walker pointer
    const char* current = trimmed_input;
    
    while (*current) {
        // when a pipe character delimits a segment
        if (*current == '|') {
            // compute length of the raw segment
            size_t seg_len = (size_t)(current - start);
            // allocate a buffer to copy the raw segment
            char* segment = malloc(seg_len + 1);
            // validate allocation
            if (!segment) {
                // free all previously allocated segment strings
                for (int j = 0; j < seg_count; j++) free(segments[j]);
                // free the segments array
                free(segments);
                handle_error(ERROR_MALLOC_FAILED, "segment in split_by_pipes");
                // abort splitting
                return NULL;
            }
            // copy the raw slice and null-terminate it
            strncpy(segment, start, seg_len); segment[seg_len] = '\0';
            char* trimmed_seg = trim_whitespace(segment);
            // if the trimmed segment is empty, that's a syntax error we already try to catch, but double-check here
            if (*trimmed_seg == '\0') {
                // free the temporary segment storage
                free(segment);
                fprintf(stderr, "Error: Empty command between pipes\n");
                // free any previous segments
                for (int j = 0; j < seg_count; j++) free(segments[j]);
                // free the segments pointer array
                free(segments);
                // stop processing
                return NULL;
            }
            // allocate a compacted buffer exactly the size of the trimmed segment
            segments[seg_count] = malloc(strlen(trimmed_seg) + 1);
            if (!segments[seg_count]) {
                // free the temporary raw buffer
                free(segment);
                // free already-built segments
                for (int j = 0; j < seg_count; j++) free(segments[j]);
                // free the segments array
                free(segments);
                // report allocation error
                handle_error(ERROR_MALLOC_FAILED, "segment copy");
                // abort
                return NULL;
            }
            // copy trimmed text into the final segment slot
            strcpy(segments[seg_count], trimmed_seg);
            // release the temporary raw buffer
            free(segment);
            // increment the segment count
            seg_count++;
            // set start to first character after the pipe for the next segment
            start = current + 1;
        }
        current++;
    }
    
    // handle the final segment after the last pipe (or the only segment if syntax slipped here)
    size_t seg_len = (size_t)(current - start);
    char* segment = malloc(seg_len + 1);
    if (!segment) {
        // free any prior segments
        for (int j = 0; j < seg_count; j++) free(segments[j]);
        // free the segments array
        free(segments);
        // report allocation error
        handle_error(ERROR_MALLOC_FAILED, "last segment");
        // abort
        return NULL;
    }
    // copy the tail slice and terminate
    strncpy(segment, start, seg_len); segment[seg_len] = '\0';
    // trim the final segment
    char* trimmed_seg = trim_whitespace(segment);
    // allocate exact-size storage for the final trimmed segment
    segments[seg_count] = malloc(strlen(trimmed_seg) + 1);
    if (!segments[seg_count]) {
        // free the temporary raw buffer
        free(segment);
        // free prior segments
        for (int j = 0; j < seg_count; j++) free(segments[j]);
        // free the segments array
        free(segments);
        // report allocation error
        handle_error(ERROR_MALLOC_FAILED, "last segment copy");
        // abort
        return NULL;
    }
    // copy final trimmed text
    strcpy(segments[seg_count], trimmed_seg);
    // free the temporary raw buffer
    free(segment);
    // increment total segments
    seg_count++;
    
    // store the number of segments produced in the out parameter
    *num_segments = seg_count;
    // return the array of segment strings
    return segments;
}

// parse an entire command line that may include pipes into a pipeline_t structure
pipeline_t* parse_pipeline(char* input) {
    // allocate the pipeline container
    pipeline_t* pipeline = malloc(sizeof(pipeline_t));
    // validate allocation
    if (!pipeline) { handle_error(ERROR_MALLOC_FAILED, "parse_pipeline"); return NULL; }
    // initialize fields to defaults
    pipeline->commands = NULL; pipeline->num_commands = 0;
    pipeline->input_file = NULL; pipeline->output_file = NULL; pipeline->error_file = NULL;
    
    // if there are no pipe symbols, treat as a single command pipeline
    if (!strchr(input, '|')) {
        // indicate one command
        pipeline->num_commands = 1;
        // allocate array for one command pointer
        pipeline->commands = malloc(sizeof(command_t*));
        // validate allocation
        if (!pipeline->commands) { free(pipeline); handle_error(ERROR_MALLOC_FAILED, "pipeline commands"); return NULL; }
        // parse the whole input as a single command
        pipeline->commands[0] = parse_command_line(input);
        // verify parse success
        if (!pipeline->commands[0]) { free(pipeline->commands); free(pipeline); return NULL; }
        // return the single-command pipeline
        return pipeline;
    }
    
    // split the input into piped segments
    int num_segments = 0;
    char** segments = split_by_pipes(input, &num_segments);
    if (!segments) { free(pipeline); return NULL; }
    
    // record number of commands
    pipeline->num_commands = num_segments;
    // allocate array of command pointers sized for the number of segments
    pipeline->commands = malloc(num_segments * sizeof(command_t*));
    // validate allocation
    if (!pipeline->commands) {
        // free segment strings and the array
        for (int i = 0; i < num_segments; i++) free(segments[i]);
        free(segments);
        // free the pipeline container
        free(pipeline);
        // report allocation error
        handle_error(ERROR_MALLOC_FAILED, "pipeline commands array");
        // abort
        return NULL;
    }
    
    // parse each segment into a command_t
    for (int i = 0; i < num_segments; i++) {
        // parse current segment
        pipeline->commands[i] = parse_command_line(segments[i]);
        // check for parse failure
        if (!pipeline->commands[i]) {
            // free any previously parsed commands
            for (int j = 0; j < i; j++) free_command(pipeline->commands[j]);
            // free the command pointer array
            free(pipeline->commands);
            // free all segments and the array
            for (int j = 0; j < num_segments; j++) free(segments[j]);
            free(segments);
            // free the pipeline container
            free(pipeline);
            // abort
            return NULL;
        }
        // mark this command as belonging to a pipeline and save its position
        pipeline->commands[i]->is_piped = 1;
        pipeline->commands[i]->pipe_position = i;
        
        // for the first command in the pipeline, move any input redirection to the pipeline level
        if (i == 0 && pipeline->commands[i]->has_input_redir) {
            // transfer ownership of the filename string
            pipeline->input_file = pipeline->commands[i]->input_file;
            // clear the command's pointer so it won't be freed twice
            pipeline->commands[i]->input_file = NULL;
            // clear the command-level flag
            pipeline->commands[i]->has_input_redir = 0;
        }
        
        // for the last command, we move output and error redirections to the pipeline level
        if (i == num_segments - 1) {
            // move output redirection
            if (pipeline->commands[i]->has_output_redir) {
                pipeline->output_file = pipeline->commands[i]->output_file;
                pipeline->commands[i]->output_file = NULL;
                pipeline->commands[i]->has_output_redir = 0;
            }
            // move error redirection
            if (pipeline->commands[i]->has_error_redir) {
                pipeline->error_file = pipeline->commands[i]->error_file;
                pipeline->commands[i]->error_file = NULL;
                pipeline->commands[i]->has_error_redir = 0;
            }
        }
        
        // for middle commands, ensure they do not attempt input/output redirection
        if (i > 0 && i < num_segments - 1) {
            // if invalid redirection is detected on a middle stage --> error
            if (pipeline->commands[i]->has_input_redir || pipeline->commands[i]->has_output_redir) {
                // inform the user about invalid syntax for pipeline middles
                fprintf(stderr, "Error: Middle command in pipeline cannot have input/output redirections\n");
                // free all commands parsed up to and including this one
                for (int k = 0; k <= i; k++) free_command(pipeline->commands[k]);
                // free the command pointer array
                free(pipeline->commands);
                // free all segments
                for (int j = 0; j < num_segments; j++) free(segments[j]);
                free(segments);
                // free pipeline container
                free(pipeline);
                // abort parsing
                return NULL;
            }
        }
    }
    
    // free segment strings after successful parsing
    for (int i = 0; i < num_segments; i++) free(segments[i]);
    // free the segment pointer array
    free(segments);
    // return the built pipeline
    return pipeline;
}

// execute a parsed pipeline by creating pipes, forking children, wiring fds, and waiting
int execute_pipeline(pipeline_t* pipeline) {
    if (!pipeline || pipeline->num_commands == 0) { handle_error(ERROR_INVALID_COMMAND, "empty pipeline"); return -1; }
    
    // handle the degenerate case of a single command without creating any pipe()
    if (pipeline->num_commands == 1) {
        // if the pipeline requested input redirection, transfer it to the single command and clear pipeline storage
        if (pipeline->input_file) { pipeline->commands[0]->input_file = pipeline->input_file; pipeline->commands[0]->has_input_redir = 1; pipeline->input_file = NULL; }
        if (pipeline->output_file) { pipeline->commands[0]->output_file = pipeline->output_file; pipeline->commands[0]->has_output_redir = 1; pipeline->output_file = NULL; }
        if (pipeline->error_file) { pipeline->commands[0]->error_file = pipeline->error_file; pipeline->commands[0]->has_error_redir = 1; pipeline->error_file = NULL; }
        return execute_simple_command(pipeline->commands[0]);
    }
    
    int num_pipes = pipeline->num_commands - 1;
    // declare a variable-length array to hold pipe file descriptors [read, write] for each pipe
    int pipes[num_pipes][2];
    pid_t pids[pipeline->num_commands];
    
    // create all pipes up front
    for (int i = 0; i < num_pipes; i++) {
        // attempt to create a pipe
        if (pipe(pipes[i]) == -1) {
            // report pipe creation failure
            handle_error(ERROR_PIPE_FAILED, "pipe creation");
            // close any previously created pipes to avoid leaks
            for (int j = 0; j < i; j++) { close(pipes[j][0]); close(pipes[j][1]); }
            // abort execution
            return -1;
        }
    }
    
    // iterate over commands, forking one child for each and wiring up stdin/stdout appropriately
    for (int i = 0; i < pipeline->num_commands; i++) {
        // fork a child process for command i
        pids[i] = fork();
        
        // handle fork failure
        if (pids[i] == -1) {
            // report fork error, include command name where possible
            handle_error(ERROR_FORK_FAILED, pipeline->commands[i]->argv ? pipeline->commands[i]->argv[0] : "(null)");
            // close all pipe fds since we are aborting
            for (int j = 0; j < num_pipes; j++) { close(pipes[j][0]); close(pipes[j][1]); }
            // reap any children already successfully forked to avoid zombies
            for (int j = 0; j < i; j++) waitpid(pids[j], NULL, 0);
            // abort
            return -1;
        // child branch
        } else if (pids[i] == 0) {
            // if this is the first command, connect stdin to pipeline input file and stdout to first pipe's write end
            if (i == 0) {
                // if a pipeline-level input file was specified, open and dup it to stdin
                if (pipeline->input_file) {
                    int fd = open(pipeline->input_file, O_RDONLY);
                    if (fd == -1) { handle_error(ERROR_FILE_NOT_FOUND, pipeline->input_file); exit(EXIT_FAILURE); }
                    if (dup2(fd, STDIN_FILENO) == -1) { handle_error(ERROR_DUP2_FAILED, "input redirection in pipeline"); close(fd); exit(EXIT_FAILURE); }
                    close(fd);
                }
                // connect stdout to the write end of the first pipe in the chain
                if (dup2(pipes[0][1], STDOUT_FILENO) == -1) { handle_error(ERROR_DUP2_FAILED, "pipe output"); exit(EXIT_FAILURE); }
            // if this is a middle command, connect stdin to previous pipe's read end and stdout to next pipe's write end
            } else if (i < pipeline->num_commands - 1) {
                if (dup2(pipes[i-1][0], STDIN_FILENO) == -1) { handle_error(ERROR_DUP2_FAILED, "pipe input"); exit(EXIT_FAILURE); }
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1) { handle_error(ERROR_DUP2_FAILED, "pipe output"); exit(EXIT_FAILURE); }
            // if this is the last command, connect stdin to previous pipe and optionally redirect stdout and/or stderr to files
            } else {
                if (dup2(pipes[i-1][0], STDIN_FILENO) == -1) { handle_error(ERROR_DUP2_FAILED, "pipe input"); exit(EXIT_FAILURE); }
                // if an output file was specified for the overall pipeline, open and dup it onto stdout
                if (pipeline->output_file) {
                    // open the target file for writing -- create/truncate
                    int fd = open(pipeline->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    // check open success
                    if (fd == -1) { handle_error(ERROR_PERMISSION_DENIED, pipeline->output_file); exit(EXIT_FAILURE); }
                    // duplicate onto stdout
                    if (dup2(fd, STDOUT_FILENO) == -1) { handle_error(ERROR_DUP2_FAILED, "output redirection in pipeline"); close(fd); exit(EXIT_FAILURE); }
                    // close the original descriptor
                    close(fd);
                }
                // if an error file was specified for the pipeline, open and dup it onto stderr
                if (pipeline->error_file) {
                    // open the target file for error output
                    int efd = open(pipeline->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (efd == -1) { handle_error(ERROR_PERMISSION_DENIED, pipeline->error_file); exit(EXIT_FAILURE); }
                    if (dup2(efd, STDERR_FILENO) == -1) { handle_error(ERROR_DUP2_FAILED, "error redirection in pipeline"); close(efd); exit(EXIT_FAILURE); }
                    // close the original descriptor
                    close(efd);
                }
            }
            
            // handle per-command stderr redirection even inside a pipeline
            if (pipeline->commands[i]->has_error_redir && pipeline->commands[i]->error_file) {
                // open the command-specific error file
                int efd = open(pipeline->commands[i]->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (efd == -1) { handle_error(ERROR_PERMISSION_DENIED, pipeline->commands[i]->error_file); exit(EXIT_FAILURE); }
                if (dup2(efd, STDERR_FILENO) == -1) { handle_error(ERROR_DUP2_FAILED, "per-command error redirection"); close(efd); exit(EXIT_FAILURE); }
                close(efd);
            }
            
            // in the child, close all pipe fds -- we keep only the dup2'ed ones
            for (int j = 0; j < num_pipes; j++) { close(pipes[j][0]); close(pipes[j][1]); }
            
            // check if this is a built-in command
            if (is_builtin_command(pipeline->commands[i])) {
                int result = 0;
                if (strcmp(pipeline->commands[i]->argv[0], "echo") == 0) {
                    result = builtin_echo(pipeline->commands[i]);
                }
                exit(result);
            }
            
            // execute the actual command for this pipeline stage
            if (execvp(pipeline->commands[i]->argv[0], pipeline->commands[i]->argv) == -1) {
                // provide nicer messages for common failures
                if (errno == ENOENT) fprintf(stderr, "Command not found: %s\n", pipeline->commands[i]->argv[0]);
                else if (errno == EACCES) fprintf(stderr, "Permission denied: %s\n", pipeline->commands[i]->argv[0]);
                else handle_error(ERROR_EXEC_FAILED, pipeline->commands[i]->argv[0]);
                // terminate child on failure to exec
                exit(EXIT_FAILURE);
            }
        }
    }
    
    // in the parent, close all pipe file descriptors as they are no longer needed here
    for (int i = 0; i < num_pipes; i++) { close(pipes[i][0]); close(pipes[i][1]); }
    
    // wait for all child processes and propagate the last command's exit status
    int status, last_status = 0;
    // iterate over child pids in order
    for (int i = 0; i < pipeline->num_commands; i++) {
        // wait for the ith child to finish
        if (waitpid(pids[i], &status, 0) == -1) { handle_error(ERROR_INVALID_COMMAND, "wait failed in pipeline"); return -1; }
        // capture the last child's exit status if it exited normally
        if (i == pipeline->num_commands - 1) last_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    
    // return the exit status of the final command in the pipeline
    return last_status;
}
