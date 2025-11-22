// client.c -- Phase 2 Client Implementation (Person B)
// client connects to the remote shell server via TCP socket
// presents a shell prompt to the user, sends commands to server
// receives and displays output from server
// provides clean user experience matching Phase 1 shell interface

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// socket programming headers
#include <sys/socket.h>  // socket(), connect(), send(), recv()
#include <netinet/in.h>  // struct sockaddr_in
#include <arpa/inet.h>   // inet_pton(), htons()

// system headers
#include <errno.h>       // errno for error handling
#include <fcntl.h>


// CLIENT configuration constants (must match server protocol)
#define PORT 8080                 // server port (must match server)
#define BUFFER_SIZE 4096          // buffer size for send/receive (must match server)
#define SERVER_IP "127.0.0.1"     // default server IP (localhost)


// function prototypes

// socket management functions
int create_and_connect_socket(const char* server_ip, int port);

// user interface and command handling
void run_client_loop(int socket_fd);
int is_empty_or_whitespace(const char* str);

// error handling functions
void print_connection_error(const char* server_ip, int port);
void print_connection_lost_error(void);


// client entry point
int main(int argc, char* argv[]) {
    // parse command line arguments for server IP and port (optional)
    const char* server_ip = SERVER_IP;  // default to localhost
    int port = PORT;                     // default to 8080

    // allow user to specify server IP and port as command line arguments
    // Usage: ./client [server_ip] [port]
    if (argc >= 2) {
        server_ip = argv[1];  // use provided IP
    }
    if (argc >= 3) {
        port = atoi(argv[2]);  // use provided port
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Error: Invalid port number. Using default port %d\n", PORT);
            port = PORT;
        }
    }

    // create socket and connect to server
    int socket_fd = create_and_connect_socket(server_ip, port);
    if (socket_fd == -1) {
        // connection failed - error already printed
        return EXIT_FAILURE;
    }

    // connection successful - enter main client loop
    // presents prompt, reads commands, sends to server, displays results
    run_client_loop(socket_fd);

    // cleanup - close socket connection
    close(socket_fd);

    return EXIT_SUCCESS;
}


// socket creation and connection
// creates a TCP socket and connects to the specified server
// returns: socket file descriptor on success, -1 on failure
int create_and_connect_socket(const char* server_ip, int port) {
    int socket_fd;
    struct sockaddr_in server_addr;

    // create TCP socket (AF_INET = IPv4, SOCK_STREAM = TCP)
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror("Error: Socket creation failed");
        return -1;
    }

    // configure server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;           // IPv4
    server_addr.sin_port = htons(port);         // convert port to network byte order

    // convert IP address from string to binary form
    // inet_pton converts "127.0.0.1" to binary representation
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid server IP address: %s\n", server_ip);
        close(socket_fd);
        return -1;
    }

    // connect to server
    // this blocks until connection is established or fails
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        // connection failed - server might not be running
        print_connection_error(server_ip, port);
        close(socket_fd);
        return -1;
    }

    // connection successful
    return socket_fd;
}


// main client loop
// presents shell prompt, reads user commands, sends to server, displays output
// continues until user types "exit" or connection is lost
void run_client_loop(int socket_fd) {
    char command[BUFFER_SIZE];    // buffer for user input
    char response[BUFFER_SIZE];   // buffer for server response
    ssize_t bytes_received;

    // main command loop - runs indefinitely until exit or disconnect
    while (1) {
        // display shell prompt (exactly like Phase 1 - clean, no extra text)
        printf("$ ");
        fflush(stdout);  // ensure prompt is displayed immediately

        // read command from user input
        // fgets reads a line including the newline character
        if (fgets(command, sizeof(command), stdin) == NULL) {
            // EOF encountered (Ctrl+D) or read error
            // send exit command to server and disconnect gracefully
            const char* exit_cmd = "exit\n";
            send(socket_fd, exit_cmd, strlen(exit_cmd), 0);
            break;
        }

        // check if command is empty or only whitespace
        // don't send empty commands to server - just show prompt again
        if (is_empty_or_whitespace(command)) {
            continue;  // skip to next iteration, display prompt again
        }

        // send command to server
        // command already includes newline from fgets (per protocol)
        ssize_t bytes_sent = send(socket_fd, command, strlen(command), 0);
        if (bytes_sent == -1) {
            // send failed - connection might be lost
            perror("Error: Failed to send command to server");
            print_connection_lost_error();
            break;
        }

        // check if user wants to exit
        // compare first 4 characters to handle "exit" or "exit\n"
        if (strncmp(command, "exit", 4) == 0) {
            // user requested exit - receive acknowledgment and break
            // server sends empty response for exit command
            recv(socket_fd, response, sizeof(response) - 1, 0);
            break;  // exit the loop, closing connection
        }

        // receive response from server
        // server sends command output (may be empty, single line, or multiple lines)
        memset(response, 0, sizeof(response));  // clear buffer
        bytes_received = recv(socket_fd, response, sizeof(response) - 1, 0);

        // check for errors or connection closed
        if (bytes_received < 0) {
            // recv error - network issue
            perror("Error: Failed to receive response from server");
            print_connection_lost_error();
            break;
        } else if (bytes_received == 0) {
            // server closed connection
            print_connection_lost_error();
            break;
        }

        // null-terminate the received data
        response[bytes_received] = '\0';

        // display server response to user
        // output is displayed exactly as received (clean, no extra formatting)
        // this preserves all newlines, spacing, and formatting from the command output
        printf("%s", response);

        // handle large outputs - keep receiving until no more data is immediately available
        // temporarily set socket to non-blocking mode
        int flags = fcntl(socket_fd, F_GETFL, 0);
        fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
        
        while (1) {
            // clear buffer for next chunk
            memset(response, 0, sizeof(response));
            
            // try to receive more data (non-blocking because we set O_NONBLOCK)
            ssize_t additional_bytes = recv(socket_fd, response, sizeof(response) - 1, 0);
            
            if (additional_bytes > 0) {
                // more data available - display it
                response[additional_bytes] = '\0';
                printf("%s", response);
                
                // update bytes_received to track the last chunk
                bytes_received = additional_bytes;
            } else if (additional_bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // no more data available right now - this is normal, we're done receiving
                break;
            } else if (additional_bytes == 0) {
                // connection closed by server
                // restore blocking mode before returning
                fcntl(socket_fd, F_SETFL, flags);
                print_connection_lost_error();
                return;
            } else {
                // real error occurred
                perror("Error: Failed to receive response from server");
                // restore blocking mode before returning
                fcntl(socket_fd, F_SETFL, flags);
                print_connection_lost_error();
                return;
            }
        }
        
        // restore socket to blocking mode for next command
        fcntl(socket_fd, F_SETFL, flags);

        // ensure output ends with newline for clean formatting
        // only add newline if the response was non-empty and doesn't already end with one
        if (bytes_received > 0 && response[bytes_received - 1] != '\n') {
            printf("\n");
        }
    }
}


//


// utility function to check if string is empty or contains only whitespace
// returns: 1 if empty/whitespace only, 0 otherwise
int is_empty_or_whitespace(const char* str) {
    if (str == NULL || str[0] == '\0') {
        return 1;  // NULL or empty string
    }

    // check if string contains only whitespace characters
    for (int i = 0; str[i] != '\0'; i++) {
        // if we find a non-whitespace character, string is not empty
        if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n' && str[i] != '\r') {
            return 0;  // found non-whitespace character
        }
    }

    return 1;  // only whitespace found
}


// error handling functions
// print user-friendly error messages

// prints connection error when unable to connect to server
void print_connection_error(const char* server_ip, int port) {
    fprintf(stderr, "Error: Cannot connect to server at %s:%d\n", server_ip, port);
    fprintf(stderr, "Please ensure the server is running and the IP/port are correct.\n");
    fprintf(stderr, "To start the server: ./server\n");
}

// prints error when connection to server is lost during operation
void print_connection_lost_error(void) {
    fprintf(stderr, "\nError: Connection to server lost.\n");
    fprintf(stderr, "The server may have crashed or the network connection was interrupted.\n");
}


