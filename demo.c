#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: demo <seconds>\n");
        return EXIT_FAILURE;
    }

    int seconds = atoi(argv[1]);
    if (seconds <= 0) {
        fprintf(stderr, "demo: seconds must be positive.\n");
        return EXIT_FAILURE;
    }

    for (int i = 1; i <= seconds; ++i) {
        printf("[demo] iteration %d/%d\n", i, seconds);
        fflush(stdout);
        sleep(1);
    }

    return EXIT_SUCCESS;
}
