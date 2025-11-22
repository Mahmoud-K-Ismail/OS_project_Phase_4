#include "shell_utils.h"

// these values will be global constants -- as per assignment
// for now, not outputting any welcome message or exit message
const char* SHELL_PROMPT = "$ ";
const char* EXIT_COMMAND = "exit";


// main function for implementing the shell loop
// shell -- displays prompt, reads input, parses and executes commands
// continues until user types 'exit'
int main(void) {

    char input[MAX_INPUT_SIZE];

    // to hold parsed commands
    pipeline_t* pipeline;
    // to check whether execution was successful
    int status;

    // welcome message and exit instructions -- commenting it out since not required -- go directly to prompt
    // printf("Custom Shell\n");
    // printf("Type 'exit' to quit.\n\n");

    // main shell loop
    while (1) {

        // display prompt and flush output buffer
        // once prompt appears, users should be able to add commands -- same line
        printf("%s", SHELL_PROMPT);
        fflush(stdout);

        // need to read input from user
        if (fgets(input, sizeof(input), stdin) == NULL) {
            // handle EOF i.e Ctrl+D -- to exit
            printf("\n");
            break;
        }

        // remove trailing newline character
        input[strcspn(input, "\n")] = '\0';

        // skip empty input or whitespace only input
        if (is_empty_string(input)) {
            continue;
        }

        // handle exit command if user wants to quit
        if (strcmp(trim_whitespace(input), EXIT_COMMAND) == 0) {
            // commenting out exit message since not required -- go directly to exit
            // printf("Goodbye, Exiting...\n");
            break;
        }
        
        // parse the command line (handles both simple commands and pipes)
        pipeline = parse_pipeline(input);
        if (pipeline == NULL) {
            // parsing failed -- possible syntax error
            handle_error(ERROR_INVALID_COMMAND, input);

            // should not execute
            continue;
        }

        // execute the pipeline (works for single commands too)
        status = execute_pipeline(pipeline);
        if (status != 0) {
            // error already handled in execute_pipeline
        }

        // clean up memory
        free_pipeline(pipeline);
    }

    return 0;
}
