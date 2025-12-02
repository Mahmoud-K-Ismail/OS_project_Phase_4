// demo.c -- Demo Program for Task Scheduler
// Simple program that runs for a specified number of seconds,
// printing progress messages each second. Used by the scheduler
// to simulate program tasks with known execution times.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Main entry point for demo program
// Takes one argument: number of seconds to run
// Prints "Demo X/N" each second where X is current iteration and N is total
// Parameters:
//   argc - Number of command line arguments
//   argv - Command line arguments (argv[1] should be number of seconds)
// Returns: EXIT_SUCCESS on success, EXIT_FAILURE on error
int main(int argc, char* argv[]) {
    // Validate command line arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: demo <seconds>\n");
        return EXIT_FAILURE;
    }

    // Parse and validate number of seconds
    int seconds = atoi(argv[1]);
    if (seconds <= 0) {
        fprintf(stderr, "demo: seconds must be positive.\n");
        return EXIT_FAILURE;
    }

    // Main loop: print progress message and sleep for each second
    for (int i = 1; i <= seconds; ++i) {
        // Print progress message in format "Demo X/N"
        // This format matches the expected output for the scheduler
        printf("Demo %d/%d\n", i, seconds);
        fflush(stdout);  // Ensure output is immediately visible
        
        // Sleep for 1 second to simulate work
        sleep(1);
    }

    return EXIT_SUCCESS;
}
