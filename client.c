// client.c -- Phase 4 Client Implementation
// client connects to the remote shell server via TCP socket
// presents a shell prompt to the user, sends commands to server
// receives and displays output from server
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
    
    // Print connection message matching assignment format
    printf("Connected to a server\n");
    
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
        // display shell prompt - CHANGED TO >>> to match assignment
        printf(">>> ");
        fflush(stdout);
        
        // read command from user input
        if (fgets(command, sizeof(command), stdin) == NULL) {
            const char* exit_cmd = "exit\n";
            send(socket_fd, exit_cmd, strlen(exit_cmd), 0);
            break;
        }
        
        // check if command is empty or only whitespace
        if (is_empty_or_whitespace(command)) {
            continue;
        }
        
        // send command to server
        ssize_t bytes_sent = send(socket_fd, command, strlen(command), 0);
        if (bytes_sent == -1) {
            perror("Error: Failed to send command to server");
            print_connection_lost_error();
            break;
        }
        
        // check if user wants to exit
        if (strncmp(command, "exit", 4) == 0) {
            recv(socket_fd, response, sizeof(response) - 1, 0);
            break;
        }
        
        // receive response from server
        // First read: blocking without timeout (wait for first response)
        memset(response, 0, sizeof(response));
        bytes_received = recv(socket_fd, response, sizeof(response) - 1, 0);
        
        if (bytes_received < 0) {
            perror("Error: Failed to receive response from server");
            print_connection_lost_error();
            break;
        } else if (bytes_received == 0) {
            print_connection_lost_error();
            break;
        }
        
        // null-terminate and display first chunk
        response[bytes_received] = '\0';
        printf("%s", response);
        
        // Now set timeout for additional data
        struct timeval tv;
        if (strncmp(command, "./demo", 6) == 0 || strncmp(command, "demo", 4) == 0) {
            tv.tv_sec = 30;    // Initial: 30 seconds (handles scheduling delays)
            tv.tv_usec = 0;
        } else {
            tv.tv_sec = 0;      // Shell commands: 300ms timeout
            tv.tv_usec = 300000;
        }
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        // Keep receiving additional data until timeout
        while (1) {
            memset(response, 0, sizeof(response));
            bytes_received = recv(socket_fd, response, sizeof(response) - 1, 0);
            
            if (bytes_received > 0) {
                // Got more data
                response[bytes_received] = '\0';
                
                // Check for end-of-task marker
                if (strstr(response, "<<END>>") != NULL) {
                    // Remove the marker from output
                    char* marker_pos = strstr(response, "<<END>>");
                    *marker_pos = '\0';  // Truncate at marker
                    if (strlen(response) > 0) {
                        printf("%s", response);
                    }
                    // Task complete, stop receiving
                    break;
                }
                
                // Normal output, display it
                printf("%s", response);
            } else if (bytes_received == 0) {
                // Server closed connection
                break;
            } else if (bytes_received == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Timeout - no more data
                    break;
                } else {
                    // Real error
                    perror("Error: Failed to receive response from server");
                    break;
                }
            }
        }
        
        // Clear timeout
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
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