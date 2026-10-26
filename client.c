#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

/*
 * =====================================================================================
 *  CS-513 System Software - Banking Management System
 *  Client Terminal Interface
 * =====================================================================================
 *  Key System Programming Concepts Implemented:
 *  1. Socket Programming:
 *     - Establishes a TCP connection (AF_INET, SOCK_STREAM) to the banking server
 *       running on port 8080.
 *  2. Application-Layer Protocol & Message Parsing:
 *     - Synchronous Request-Response protocol using 4096-byte buffers.
 *     - Caret ('^') delimiter indicates an informational alert or confirmation message
 *       from the server. The client strips the caret, displays the message, and returns
 *       an empty acknowledgement frame ("") to synchronize the communication state.
 *     - "Client logging out...\n" message signals an orderly session disconnect.
 *  3. Terminal I/O Control (termios API):
 *     - Modifies POSIX terminal line settings via tcgetattr() and tcsetattr().
 *     - Masks password input by clearing the ECHO bit in c_lflag so sensitive credentials
 *       are not displayed on screen.
 * =====================================================================================
 */

void connectionHandler(int socketFileDescriptor);
void hide_input(char *buffer, int size);

int main() {
    int socketFileDescriptor;
    int connectStatus;
    struct sockaddr_in address;

    // 1. Create client-side stream socket (TCP/IP)
    socketFileDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFileDescriptor == -1) {
        perror("[ERROR] Client socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("[CLIENT] Socket successfully created.\n");

    // Configure server connection parameters
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY); // Localhost
    address.sin_port = htons(8080);              // Banking server port

    // 2. Initiate TCP handshake to server
    printf("[CLIENT] Connecting to Bank Management Server on port 8080...\n");
    connectStatus = connect(socketFileDescriptor, (struct sockaddr *)&address, sizeof(address));
    if (connectStatus == -1) {
        perror("[ERROR] Unable to connect to Bank Server. Is the server running?");
        close(socketFileDescriptor);
        exit(EXIT_FAILURE);
    }
    printf("[CLIENT] Successfully connected to Bank Server!\n");

    // 3. Enter interactive session loop
    connectionHandler(socketFileDescriptor);

    close(socketFileDescriptor);
    return 0;
}

/**
 * connectionHandler - Primary state machine managing server transmissions and user inputs.
 */
void connectionHandler(int socketFileDescriptor) {
    char readBuffer[4096];
    char writeBuffer[4096];
    char tempBuffer[4096];
    int readBytes, writeBytes;

    do {
        bzero(readBuffer, sizeof(readBuffer));
        readBytes = read(socketFileDescriptor, readBuffer, sizeof(readBuffer));

        if (readBytes == -1) {
            perror("[ERROR] Read error from server socket");
            break;
        } else if (readBytes == 0) {
            printf("\n[CLIENT] Server closed the connection.\n");
            break;
        } else {
            // Case A: Server requests password entry (mask user input)
            if (strcmp(readBuffer, "Enter password: ") == 0 ||
                strcmp(readBuffer, "Enter Initial Password: ") == 0 ||
                strcmp(readBuffer, "Enter new admin password: ") == 0) {
                hide_input(writeBuffer, sizeof(writeBuffer));
            } else {
                bzero(writeBuffer, sizeof(writeBuffer));
                bzero(tempBuffer, sizeof(tempBuffer));

                // Case B: Server signals graceful termination
                if (strcmp(readBuffer, "Client logging out...\n") == 0) {
                    printf("%s", readBuffer);
                    strcpy(writeBuffer, "");
                    write(socketFileDescriptor, writeBuffer, sizeof(writeBuffer));
                    close(socketFileDescriptor);
                    return;
                }
                // Case C: Message contains '^' delimiter (notification/acknowledgment)
                else if (strchr(readBuffer, '^') != NULL) {
                    if (strlen(readBuffer) > 1) {
                        // Strip trailing '^' character before printing
                        strncpy(tempBuffer, readBuffer, strlen(readBuffer) - 1);
                        printf("%s\n", tempBuffer);
                    }
                    // Return empty string as acknowledgment frame
                    strcpy(writeBuffer, "");
                }
                // Case D: Standard prompt requiring user input
                else {
                    printf("%s", readBuffer);
                    scanf("%s", writeBuffer);
                }
            }

            // Transmit user input or ack packet to server
            writeBytes = write(socketFileDescriptor, writeBuffer, sizeof(writeBuffer));
            if (writeBytes == -1) {
                perror("[ERROR] Failed to write data to server");
                break;
            }
        }
    } while (readBytes > 0);

    close(socketFileDescriptor);
}

/**
 * hide_input - Suppresses terminal echo during password input using POSIX termios API.
 */
void hide_input(char *buffer, int size) {
    struct termios oldt, newt;

    // Retrieve current terminal attributes for standard input
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;

    // Clear ECHO flag to prevent typed characters from displaying
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    // Prompt user and capture password string
    printf("Enter password: ");
    bzero(buffer, size);
    scanf("%s", buffer);
    printf("\n");

    // Restore original terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}
