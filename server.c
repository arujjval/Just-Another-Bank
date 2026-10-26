#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <netinet/ip.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <crypt.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <wait.h>
#include <semaphore.h>

/*
 * =====================================================================================
 *  CS-513 System Software - Banking Management System
 *  Server Daemon & Multi-Process Connection Coordinator
 * =====================================================================================
 *  Key System Programming Concepts Implemented:
 *  1. Socket Programming: Stream sockets (AF_INET, SOCK_STREAM) for reliable TCP IPC.
 *  2. Concurrency Model: Concurrent multi-process architecture via fork(). Each client
 *     connection is isolated in its own child process, preventing memory corruption
 *     across client sessions.
 *  3. Zombie Process Prevention: SIGCHLD is explicitly ignored (SIG_IGN), directing the
 *     Linux kernel to immediately reap terminated child processes without requiring
 *     blocking wait() calls.
 *  4. Single-Session Enforcement: POSIX named semaphores (/sem_<id> and /sem_admin)
 *     prevent simultaneous logins with the same user credentials.
 *  5. Graceful Cleanup: Signal handlers catch unexpected termination (SIGINT, SIGTERM)
 *     and automatically unlink semaphores to avoid orphaned lockouts.
 * =====================================================================================
 */

#define EMPPATH     "employees.txt"
#define CUSPATH     "customers.txt"
#define LOANPATH    "loanDetails.txt"
#define COUNTERPATH "loanCounter.txt"
#define HISTORYPATH "trans_hist.txt"
#define FEEDPATH    "feedback.txt"
#define ADMINPATH   "admin.txt"
#define HASHKEY     "$6$saltsalt$"

#define MAINMENU "\n===============================\n   BANK MANAGEMENT SYSTEM\n===============================\n1. Customer Login\t2. Employee Login\n3. Manager Login\t4. Admin Login\n5. Exit Program\nEnter option: "

#define CUSMENU "\n--- Customer Panel ---\n1. Deposit Money\t2. Withdraw Money\n3. Check Balance\t4. Apply for Loan\n5. Transfer Funds\t6. Change Account Password\n7. View Transaction History\t8. Provide Feedback\n9. Logout\t10. Exit\nEnter option: "

#define EMPMENU "\n--- Employee Dashboard ---\n1. Register New Customer\t2. Edit Customer Information\n3. Review/Approve Loan Requests\t4. View Assigned Loan Tasks\n5. Review Customer Transactions\t6. View Customer Details\n7. Delete Customer Account\t8. Change Password\n9. Logout\t10. Exit\nEnter option: "

#define MNGMENU "\n--- Manager Section ---\n1. Enable/Disable Customer Accounts\n2. Assign Loan Applications to Employees\n3. View & Review Customer Feedback\n4. Update Your Password\n5. Logout\t6. Exit\nEnter option: "

#define ADMINMENU "\n--- Admin Console ---\n1. Register New Employee\t2. Update Customer/Employee Info\n3. Manage Roles and Permissions\t4. View Customer Details\n5. View Employee Details\t6. Delete Customer Account\n7. Delete Employee Account\t8. Change Admin Password\n9. Logout\t10. Exit\nEnter option: "

// Active named semaphore tracked for the current process session
sem_t *sema = NULL;
char semName[64] = "";

// Forward declarations
void customerMenu(int connection_fd);
void employeeMenu(int connection_fd);
void managerMenu(int connection_fd);
void adminMenu(int connection_fd);
void connectionHandler(int connection_fd);
void exitClient(int connection_fd, int id, const char *customSem);
void cleanupSemaphore(int signum);
void sessionCleanup(void);
void setupSignalHandlers(void);
sem_t *initializeSemaphore(int id);
sem_t *initializeNamedSemaphore(const char *name);

/**
 * sessionCleanup - Releases and unlinks the active named semaphore for this session.
 * Guarantees that semaphores in /dev/shm are removed even if client terminates abruptly.
 */
void sessionCleanup(void) {
    if (strlen(semName) > 0) {
        sem_t *s = sem_open(semName, 0);
        if (s != SEM_FAILED) {
            sem_post(s);
            sem_close(s);
            sem_unlink(semName);
        }
        semName[0] = '\0';
    }
}

/**
 * exitClient - Gracefully terminates client session and worker process.
 * 
 * 1. Releases and unlinks active named semaphore to permit future logins.
 * 2. Transmits standard logout packet ("Client logging out...\n") to client.
 * 3. Awaits client acknowledgement, closes TCP descriptor, and calls _exit().
 */
void exitClient(int connection_fd, int id, const char *customSem) {
    char out_buf[4096];
    char in_buf[4096];

    // Identify semaphore name
    if (customSem != NULL && strlen(customSem) > 0) {
        strncpy(semName, customSem, sizeof(semName) - 1);
    } else if (id != 0) {
        snprintf(semName, sizeof(semName), "/sem_%d", id);
    }

    // Release and unlink named semaphore if held
    sessionCleanup();

    // Transmit shutdown handshake to client
    bzero(out_buf, sizeof(out_buf));
    strcpy(out_buf, "Client logging out...\n");
    write(connection_fd, out_buf, sizeof(out_buf));
    bzero(in_buf, sizeof(in_buf));
    read(connection_fd, in_buf, sizeof(in_buf));

    close(connection_fd);
    _exit(0); // Terminate forked child worker process cleanly
}

/**
 * cleanupSemaphore - Signal handler to release semaphores on unexpected shutdown.
 * Catches SIGINT, SIGTERM, and SIGPIPE (broken pipe when client aborts).
 */
void cleanupSemaphore(int signum) {
    sessionCleanup();
    printf("\n[CLEANUP] Signal %d caught. Session semaphore released.\n", signum);
    _exit(signum);
}

/**
 * initializeSemaphore - Creates or connects to a named semaphore for a numeric user ID.
 * Initialized with count 1 (binary semaphore acting as a mutex).
 */
sem_t *initializeSemaphore(int id) {
    snprintf(semName, sizeof(semName), "/sem_%d", id);
    return sem_open(semName, O_CREAT, 0644, 1);
}

/**
 * initializeNamedSemaphore - Creates or connects to an arbitrary string-named semaphore.
 */
sem_t *initializeNamedSemaphore(const char *name) {
    snprintf(semName, sizeof(semName), "%s", name);
    return sem_open(semName, O_CREAT, 0644, 1);
}

/**
 * setupSignalHandlers - Registers handlers for abnormal termination signals and broken pipes.
 */
void setupSignalHandlers(void) {
    signal(SIGINT, cleanupSemaphore);
    signal(SIGTERM, cleanupSemaphore);
    signal(SIGSEGV, cleanupSemaphore);
    signal(SIGHUP, cleanupSemaphore);
    signal(SIGQUIT, cleanupSemaphore);
    signal(SIGPIPE, cleanupSemaphore); // Critical: catch broken pipe on abrupt client disconnect
}

#include "bank_fun.h"
#include "customer_functions.h"
#include "admin_functions.h"
#include "employee_functions.h"
#include "manager_functions.h"

int main() {
    int socket_fd, connection_fd;
    int opt = 1;
    struct sockaddr_in address, client;
    socklen_t clientSize;

    // Prevent child worker processes from becoming zombies upon termination.
    // Setting SIGCHLD to SIG_IGN tells the Linux kernel to automatically clean up exit status.
    signal(SIGCHLD, SIG_IGN);

    // Register top-level signal handlers
    setupSignalHandlers();

    // Automatically purge any stale semaphores from /dev/shm from previously aborted/crashed runs
    system("rm -f /dev/shm/sem.sem_* /dev/shm/sem.* 2>/dev/null");
    sem_unlink("/sem_admin_session");
    printf("[SERVER] Cleared any stale session semaphores from /dev/shm.\n");

    // 1. Create stream socket (TCP/IP)
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("[SERVER] Socket created successfully (fd: %d)\n", socket_fd);

    // Allow immediate socket address reuse to prevent "Address already in use" errors during quick restarts
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
    }

    // Configure server address structure
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY); // Bind to all local network interfaces
    address.sin_port = htons(8080);              // Banking port

    // 2. Bind socket to designated port and interface
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("Binding failed");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }
    printf("[SERVER] Binding successful to port 8080\n");

    // 3. Place socket in passive listening mode with connection backlog queue
    if (listen(socket_fd, 10) == -1) {
        perror("Listen failed");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }
    printf("[SERVER] Server listening for concurrent incoming client connections...\n");

    // 4. Concurrent connection acceptance loop
    while (1) {
        clientSize = sizeof(client);
        connection_fd = accept(socket_fd, (struct sockaddr *)&client, &clientSize);

        if (connection_fd == -1) {
            if (errno == EINTR) continue; // Retry if interrupted by signal
            perror("Accept failed");
            continue;
        }

        printf("[SERVER] Accepted new connection from client (conn_fd: %d)\n", connection_fd);

        // Fork a dedicated worker process for each connected client (Concurrency)
        pid_t pid = fork();
        if (pid == 0) {
            // Child process: closes listening socket copy, processes client requests
            close(socket_fd);
            setupSignalHandlers();
            connectionHandler(connection_fd);
            sessionCleanup(); // Release and unlink named semaphore on disconnect
            close(connection_fd);
            _exit(0);
        } else if (pid > 0) {
            // Parent process: closes connected client socket copy and continues listening
            close(connection_fd);
        } else {
            perror("fork() failed to create client process");
            close(connection_fd);
        }
    }

    close(socket_fd);
    return 0;
}

/**
 * connectionHandler - Primary dispatch loop for an established client session.
 * Presents the role selection menu and dispatches to role-specific controller functions.
 */
void connectionHandler(int connection_fd) {
    char readBuffer[4096], write_buf[4096];
    int readBytes, writeBytes, choice;

    while (1) {
        // Send main login selection menu
        writeBytes = write(connection_fd, MAINMENU, sizeof(MAINMENU));
        if (writeBytes <= 0) {
            printf("[SERVER] Client disconnected or write failed.\n");
            return;
        }

        bzero(readBuffer, sizeof(readBuffer));
        readBytes = read(connection_fd, readBuffer, sizeof(readBuffer));
        if (readBytes <= 0) {
            printf("[SERVER] Client closed connection.\n");
            return;
        }

        choice = atoi(readBuffer);
        printf("[SERVER] Client selected menu option: %d\n", choice);

        switch (choice) {
            case 1:
                customerMenu(connection_fd);
                break;
            case 2:
                employeeMenu(connection_fd);
                break;
            case 3:
                managerMenu(connection_fd);
                break;
            case 4:
                adminMenu(connection_fd);
                break;
            case 5:
                exitClient(connection_fd, 0, NULL);
                return;
            default:
                bzero(readBuffer, sizeof(readBuffer));
                bzero(write_buf, sizeof(write_buf));
                strcpy(write_buf, "Invalid Choice for Login menu^");
                write(connection_fd, write_buf, sizeof(write_buf));
                read(connection_fd, readBuffer, sizeof(readBuffer));
                break;
        }
    }
}
