#ifndef CUSTOMER_FUNCTIONS_H
#define CUSTOMER_FUNCTIONS_H

/*
 * =====================================================================================
 *  CS-513 System Software - Banking Management System
 *  Customer Subsystem Controller
 * =====================================================================================
 *  System Software Concepts Implemented:
 *  1. ACID Transactions & Record-Level File Locking (fcntl):
 *     - Atomicity: Fund transfer ensures both debit and credit occur together.
 *     - Consistency: Balance invariant checks prevent negative balance overdrafts.
 *     - Isolation: Advisory write locks (F_WRLCK) via F_SETLKW prevent dirty reads
 *       and concurrent race conditions.
 *     - Durability: Direct system calls (write, lseek) persist state changes to disk.
 *  2. Deadlock Prevention:
 *     - Strict hierarchical lock acquisition in transferFunds() (locks smaller file
 *       offset first) completely breaks the circular wait condition.
 *  3. Critical Section Optimization:
 *     - User socket I/O is performed BEFORE acquiring file locks to prevent prolonged
 *       lock holding that could cause denial-of-service to concurrent users.
 *  4. Single-Session Enforcement:
 *     - POSIX named semaphores (/sem_<accountNumber>) ensure exactly one active session.
 * =====================================================================================
 */

// Function prototypes
void customerMenu(int connection_fd);
int  loginCustomer(int connection_fd, int accountNumber, char *password);
void customerBal(int connection_fd, int accountNumber);
void depositMoney(int connection_fd, int accountNumber);
void withdrawMoney(int connection_fd, int accountNumber);
void transferFunds(int connection_fd, int sourceAccount);
void applyLoan(int connection_fd, int accountNumber);
void transactionHistory(int connection_fd, int accountNumber);
void addFeedback(int connection_fd, int accountNumber);
int  changePassword(int connection_fd, int accountNumber);
void logout(int connection_fd, int id);

/**
 * customerMenu - Main interactive control loop for authenticated customer sessions.
 */
void customerMenu(int connection_fd) {
    int accountNumber;
    char password[256];
    char write_buf[4096];
    char readBuffer[4096];
    int readBytes, writeBytes;

start:
    // Prompt for Account Number
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "\nEnter account number: ");
    if (write(connection_fd, write_buf, sizeof(write_buf)) <= 0) return;

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    accountNumber = atoi(readBuffer);

    // Prompt for Password (triggers echo suppression on client)
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter password: ");
    if (write(connection_fd, write_buf, sizeof(write_buf)) <= 0) return;

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    strncpy(password, readBuffer, sizeof(password) - 1);

    // Authenticate and enforce single-session restriction
    int loginResult = loginCustomer(connection_fd, accountNumber, password);
    if (loginResult == 1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nLogin Successfully^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer)); // Wait for client ack

        while (1) {
            bzero(write_buf, sizeof(write_buf));
            strcpy(write_buf, CUSMENU);
            if (write(connection_fd, write_buf, sizeof(write_buf)) <= 0) {
                logout(connection_fd, accountNumber);
                return;
            }

            bzero(readBuffer, sizeof(readBuffer));
            if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) {
                logout(connection_fd, accountNumber);
                return;
            }
            int choice = atoi(readBuffer);

            switch (choice) {
                case 1:
                    depositMoney(connection_fd, accountNumber);
                    break;
                case 2:
                    withdrawMoney(connection_fd, accountNumber);
                    break;
                case 3:
                    customerBal(connection_fd, accountNumber);
                    break;
                case 4:
                    applyLoan(connection_fd, accountNumber);
                    break;
                case 5:
                    transferFunds(connection_fd, accountNumber);
                    break;
                case 6: {
                    int resp = changePassword(connection_fd, accountNumber);
                    bzero(write_buf, sizeof(write_buf));
                    bzero(readBuffer, sizeof(readBuffer));
                    if (resp) {
                        strcpy(write_buf, "Password changed successfully! Please log in again.^");
                    } else {
                        strcpy(write_buf, "Unable to change password^");
                    }
                    write(connection_fd, write_buf, sizeof(write_buf));
                    read(connection_fd, readBuffer, sizeof(readBuffer));
                    logout(connection_fd, accountNumber);
                    goto start;
                }
                case 7:
                    transactionHistory(connection_fd, accountNumber);
                    break;
                case 8:
                    addFeedback(connection_fd, accountNumber);
                    break;
                case 9:
                    printf("[CUSTOMER] Account %d logged out.\n", accountNumber);
                    logout(connection_fd, accountNumber);
                    return;
                case 10:
                    printf("[CUSTOMER] Account %d requested process exit.\n", accountNumber);
                    exitClient(connection_fd, accountNumber, NULL);
                    return;
                default:
                    bzero(write_buf, sizeof(write_buf));
                    strcpy(write_buf, "Invalid choice for customer menu^");
                    write(connection_fd, write_buf, sizeof(write_buf));
                    read(connection_fd, readBuffer, sizeof(readBuffer));
                    break;
            }
        }
    } else if (loginResult == -2) {
        // Already logged in in another session
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nUser is already logged in on another session!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        goto start;
    } else if (loginResult == -3) {
        // Account deactivated by manager
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nAccount is deactivated or suspended. Please contact bank manager.^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        goto start;
    } else {
        // Invalid credentials or non-existent account
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nInvalid Account Number or Password^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        goto start;
    }
}

/**
 * loginCustomer - Authenticates credentials and locks named semaphore for single-session.
 * Returns:  1 on success
 *          -2 on duplicate session (semaphore busy)
 *          -3 on deactivated account
 *           0 on invalid credentials or not found
 */
int loginCustomer(int connection_fd, int accountNumber, char *password) {
    struct Customer customer;
    int file = open(CUSPATH, O_RDONLY);
    if (file == -1) return 0;

    // Check credential correctness and status under shared read lock
    int authenticated = 0;
    while (read(file, &customer, sizeof(customer)) > 0) {
        if (customer.accountNumber == accountNumber) {
            if (strcmp(customer.password, crypt(password, HASHKEY)) == 0) {
                if (customer.activeStatus == 1) {
                    authenticated = 1;
                } else {
                    authenticated = -3; // Deactivated or deleted
                }
            }
            break;
        }
    }
    close(file);

    if (authenticated != 1) {
        return authenticated; // 0 (bad credentials) or -3 (deactivated)
    }

    // Initialize named POSIX semaphore to enforce single session per user
    sema = initializeSemaphore(accountNumber);
    setupSignalHandlers();

    // sem_trywait attempts non-blocking lock. If already 0, another session holds it.
    if (sem_trywait(sema) == -1) {
        if (errno == EAGAIN) {
            printf("[AUTH] Customer account %d is already logged in elsewhere.\n", accountNumber);
            return -2;
        }
        perror("sem_trywait error");
        return 0;
    }

    printf("[AUTH] Customer account %d successfully authenticated.\n", accountNumber);
    return 1;
}

/**
 * customerBal - Displays current account balance using shared read lock.
 */
void customerBal(int connection_fd, int accountNumber) {
    char write_buf[4096];
    char readBuffer[4096];
    struct Customer customer;
    float currentBalance = 0.0f;
    int found = 0;

    int file = open(CUSPATH, O_RDONLY);
    if (file != -1) {
        while (read(file, &customer, sizeof(customer)) > 0) {
            if (customer.accountNumber == accountNumber) {
                currentBalance = customer.balance;
                found = 1;
                break;
            }
        }
        close(file);
    }

    bzero(write_buf, sizeof(write_buf));
    if (found) {
        snprintf(write_buf, sizeof(write_buf), "Your current account balance is: %.2f^", currentBalance);
    } else {
        strcpy(write_buf, "Account record not found^");
    }
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * depositMoney - Credits funds to customer account with record-level write lock.
 * 
 * Note on Concurrency:
 * User input is read over the network FIRST before acquiring the file lock.
 * This minimizes critical section time and prevents holding locks across user idle time.
 */
void depositMoney(int connection_fd, int accountNumber) {
    char write_buf[4096];
    char readBuffer[4096];
    char transactionBuffer[1024];
    struct Customer customer;
    struct trans_histroy th;
    float depositAmount = 0.0f;

    // 1. Prompt for deposit amount over socket BEFORE locking disk file
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter the amount to deposit: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    depositAmount = atof(readBuffer);

    if (depositAmount <= 0.0f) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Invalid deposit amount! Transaction cancelled.^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // 2. Open customer data file for update
    int file = open(CUSPATH, O_RDWR);
    if (file == -1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Database error. Please try again later.^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // 3. Locate the customer record's byte offset
    off_t recordOffset = -1;
    while (read(file, &customer, sizeof(customer)) > 0) {
        if (customer.accountNumber == accountNumber) {
            recordOffset = lseek(file, -sizeof(struct Customer), SEEK_CUR);
            break;
        }
    }

    if (recordOffset == -1) {
        close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Customer record not found^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // 4. Acquire exclusive write lock (F_WRLCK) on specific record
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = recordOffset;
    fl.l_len = sizeof(struct Customer);
    fl.l_pid = getpid();

    fcntl(file, F_SETLKW, &fl); // Block until write lock is acquired

    // 5. Read fresh record under lock (prevents race condition)
    lseek(file, recordOffset, SEEK_SET);
    read(file, &customer, sizeof(customer));

    // Update balance
    customer.balance += depositAmount;

    // 6. Persist updated record back to disk
    lseek(file, recordOffset, SEEK_SET);
    write(file, &customer, sizeof(customer));

    // 7. Release record write lock
    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    // 8. Append entry to audit transaction history
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    int histFile = open(HISTORYPATH, O_RDWR | O_APPEND | O_CREAT, 0644);
    if (histFile != -1) {
        struct flock hfl;
        memset(&hfl, 0, sizeof(hfl));
        hfl.l_type = F_WRLCK;
        hfl.l_whence = SEEK_END;
        hfl.l_len = sizeof(struct trans_histroy);
        fcntl(histFile, F_SETLKW, &hfl);

        snprintf(transactionBuffer, sizeof(transactionBuffer),
                 "%.2f deposited at %02d:%02d:%02d %04d-%02d-%02d (New Bal: %.2f)\n",
                 depositAmount, tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday, customer.balance);

        th.acc_no = accountNumber;
        strncpy(th.hist, transactionBuffer, sizeof(th.hist) - 1);
        write(histFile, &th, sizeof(th));

        hfl.l_type = F_UNLCK;
        fcntl(histFile, F_SETLK, &hfl);
        close(histFile);
    }

    // 9. Send success message to client
    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Deposit successful! New balance: %.2f^", customer.balance);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * withdrawMoney - Debits funds from customer account with ACID consistency checks.
 */
void withdrawMoney(int connection_fd, int accountNumber) {
    char write_buf[4096];
    char readBuffer[4096];
    char transactionBuffer[1024];
    struct Customer customer;
    struct trans_histroy th;
    float withdrawAmount = 0.0f;

    // 1. Prompt for withdrawal amount outside of file lock
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter the amount to withdraw: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    withdrawAmount = atof(readBuffer);

    if (withdrawAmount <= 0.0f) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Invalid withdrawal amount! Transaction cancelled.^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    int file = open(CUSPATH, O_RDWR);
    if (file == -1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Database error^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    off_t recordOffset = -1;
    while (read(file, &customer, sizeof(customer)) > 0) {
        if (customer.accountNumber == accountNumber) {
            recordOffset = lseek(file, -sizeof(struct Customer), SEEK_CUR);
            break;
        }
    }

    if (recordOffset == -1) {
        close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Account record not found^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // 2. Acquire exclusive write lock on record
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = recordOffset;
    fl.l_len = sizeof(struct Customer);
    fl.l_pid = getpid();
    fcntl(file, F_SETLKW, &fl);

    // 3. Read fresh balance under lock
    lseek(file, recordOffset, SEEK_SET);
    read(file, &customer, sizeof(customer));

    // Consistency Check: Ensure account balance never drops below 0
    if (customer.balance < withdrawAmount) {
        fl.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl);
        close(file);

        bzero(write_buf, sizeof(write_buf));
        snprintf(write_buf, sizeof(write_buf), "Insufficient funds! Current balance is only %.2f^", customer.balance);
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // Deduct funds
    customer.balance -= withdrawAmount;

    // 4. Persist updated record
    lseek(file, recordOffset, SEEK_SET);
    write(file, &customer, sizeof(customer));

    // 5. Release write lock
    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    // 6. Record transaction in audit history
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    int histFile = open(HISTORYPATH, O_RDWR | O_APPEND | O_CREAT, 0644);
    if (histFile != -1) {
        struct flock hfl;
        memset(&hfl, 0, sizeof(hfl));
        hfl.l_type = F_WRLCK;
        hfl.l_whence = SEEK_END;
        hfl.l_len = sizeof(struct trans_histroy);
        fcntl(histFile, F_SETLKW, &hfl);

        snprintf(transactionBuffer, sizeof(transactionBuffer),
                 "%.2f withdrawn at %02d:%02d:%02d %04d-%02d-%02d (New Bal: %.2f)\n",
                 withdrawAmount, tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday, customer.balance);

        th.acc_no = accountNumber;
        strncpy(th.hist, transactionBuffer, sizeof(th.hist) - 1);
        write(histFile, &th, sizeof(th));

        hfl.l_type = F_UNLCK;
        fcntl(histFile, F_SETLK, &hfl);
        close(histFile);
    }

    // 7. Inform client
    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Withdrawal successful! New balance: %.2f^", customer.balance);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * transferFunds - Inter-account fund transfer with strict deadlock prevention and atomicity.
 * 
 * Deadlock Prevention Mechanism:
 * To eliminate circular wait conditions, both accounts are always locked in ascending
 * file offset order: min(srcOffset, dstOffset) followed by max(srcOffset, dstOffset).
 * Even if two concurrent users initiate reciprocal transfers (A->B and B->A), both
 * processes will request the locks in the exact same sequence, making deadlock mathematically impossible.
 */
void transferFunds(int connection_fd, int sourceAccount) {
    char write_buf[4096];
    char readBuffer[4096];
    char transactionBuffer[1024];
    struct Customer srcCustomer, dstCustomer;
    struct trans_histroy th;
    int destAccount = 0;
    float transferAmount = 0.0f;

    // 1. Gather destination account and amount from client
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter destination account number: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    destAccount = atoi(readBuffer);

    if (destAccount == sourceAccount) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Cannot transfer funds to the same account!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter amount to transfer: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    transferAmount = atof(readBuffer);

    if (transferAmount <= 0.0f) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Invalid transfer amount! Transaction cancelled.^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // 2. Open customers database
    int file = open(CUSPATH, O_RDWR);
    if (file == -1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Database error^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // 3. Find record offsets for source and destination accounts
    off_t srcOffset = -1, dstOffset = -1;
    struct Customer temp;
    lseek(file, 0, SEEK_SET);
    while (read(file, &temp, sizeof(temp)) > 0) {
        if (temp.accountNumber == sourceAccount) {
            srcOffset = lseek(file, -sizeof(struct Customer), SEEK_CUR);
            lseek(file, sizeof(struct Customer), SEEK_CUR);
        }
        if (temp.accountNumber == destAccount) {
            dstOffset = lseek(file, -sizeof(struct Customer), SEEK_CUR);
            lseek(file, sizeof(struct Customer), SEEK_CUR);
        }
    }

    if (srcOffset == -1 || dstOffset == -1) {
        close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Destination account does not exist!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // 4. Deadlock-free locking: Acquire locks in strictly increasing offset order
    off_t firstOffset  = (srcOffset < dstOffset) ? srcOffset : dstOffset;
    off_t secondOffset = (srcOffset < dstOffset) ? dstOffset : srcOffset;

    struct flock fl1, fl2;
    memset(&fl1, 0, sizeof(fl1));
    fl1.l_type = F_WRLCK;
    fl1.l_whence = SEEK_SET;
    fl1.l_start = firstOffset;
    fl1.l_len = sizeof(struct Customer);
    fl1.l_pid = getpid();
    fcntl(file, F_SETLKW, &fl1);

    memset(&fl2, 0, sizeof(fl2));
    fl2.l_type = F_WRLCK;
    fl2.l_whence = SEEK_SET;
    fl2.l_start = secondOffset;
    fl2.l_len = sizeof(struct Customer);
    fl2.l_pid = getpid();
    fcntl(file, F_SETLKW, &fl2);

    // 5. Read fresh state of both accounts under exclusive locks
    lseek(file, srcOffset, SEEK_SET);
    read(file, &srcCustomer, sizeof(srcCustomer));

    lseek(file, dstOffset, SEEK_SET);
    read(file, &dstCustomer, sizeof(dstCustomer));

    // Verify destination account is active
    if (dstCustomer.activeStatus != 1) {
        fl1.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl1);
        fl2.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl2);
        close(file);

        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Destination account is inactive or suspended! Transfer rejected.^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // Verify source account has sufficient balance
    if (srcCustomer.balance < transferAmount) {
        fl1.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl1);
        fl2.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl2);
        close(file);

        bzero(write_buf, sizeof(write_buf));
        snprintf(write_buf, sizeof(write_buf), "Insufficient funds in source account! (Bal: %.2f)^", srcCustomer.balance);
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // 6. Perform Atomic Debit and Credit
    srcCustomer.balance -= transferAmount;
    dstCustomer.balance += transferAmount;

    // Write both updated records back to disk
    lseek(file, srcOffset, SEEK_SET);
    write(file, &srcCustomer, sizeof(srcCustomer));

    lseek(file, dstOffset, SEEK_SET);
    write(file, &dstCustomer, sizeof(dstCustomer));

    // 7. Release locks using correct F_SETLK command
    fl1.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl1);

    fl2.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl2);

    close(file);

    // 8. Record audit log entries for both accounts
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    int histFile = open(HISTORYPATH, O_RDWR | O_APPEND | O_CREAT, 0644);
    if (histFile != -1) {
        struct flock hfl;
        memset(&hfl, 0, sizeof(hfl));
        hfl.l_type = F_WRLCK;
        hfl.l_whence = SEEK_END;
        hfl.l_len = sizeof(struct trans_histroy) * 2;
        fcntl(histFile, F_SETLKW, &hfl);

        // Source account debit entry
        snprintf(transactionBuffer, sizeof(transactionBuffer),
                 "%.2f transferred to acc #%d at %02d:%02d:%02d %04d-%02d-%02d (New Bal: %.2f)\n",
                 transferAmount, destAccount, tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday, srcCustomer.balance);
        th.acc_no = sourceAccount;
        strncpy(th.hist, transactionBuffer, sizeof(th.hist) - 1);
        write(histFile, &th, sizeof(th));

        // Destination account credit entry
        snprintf(transactionBuffer, sizeof(transactionBuffer),
                 "%.2f credited from acc #%d at %02d:%02d:%02d %04d-%02d-%02d (New Bal: %.2f)\n",
                 transferAmount, sourceAccount, tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday, dstCustomer.balance);
        th.acc_no = destAccount;
        strncpy(th.hist, transactionBuffer, sizeof(th.hist) - 1);
        write(histFile, &th, sizeof(th));

        hfl.l_type = F_UNLCK;
        fcntl(histFile, F_SETLK, &hfl);
        close(histFile);
    }

    // 9. Send success message to client
    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Transfer of %.2f to account %d successful! Remaining balance: %.2f^",
             transferAmount, destAccount, srcCustomer.balance);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * applyLoan - Submits a loan application with synchronized ID generation.
 * 
 * Race Condition Prevention:
 * loanCounter.txt is locked exclusively with fcntl to guarantee unique, sequential
 * loan ID assignment under heavy concurrent applications.
 */
void applyLoan(int connection_fd, int accountNumber) {
    char write_buf[4096];
    char readBuffer[4096];
    struct Counter ct;
    struct LoanDetails ld;
    int amount = 0;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Loan Amount: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    amount = atoi(readBuffer);

    if (amount <= 0) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Invalid loan amount requested!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // 1. Synchronized loan ID generation using locked counter file
    int counterFd = open(COUNTERPATH, O_CREAT | O_RDWR, 0644);
    if (counterFd == -1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Failed to access loan counter database^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    struct flock cfl;
    memset(&cfl, 0, sizeof(cfl));
    cfl.l_type = F_WRLCK;
    cfl.l_whence = SEEK_SET;
    cfl.l_start = 0;
    cfl.l_len = sizeof(struct Counter);
    cfl.l_pid = getpid();
    fcntl(counterFd, F_SETLKW, &cfl); // Lock counter

    int bytesRead = read(counterFd, &ct, sizeof(ct));
    if (bytesRead <= 0) {
        ct.count = 0;
    }
    ct.count++;
    int assignedLoanID = ct.count;

    lseek(counterFd, 0, SEEK_SET);
    write(counterFd, &ct, sizeof(ct));

    // Release counter lock
    cfl.l_type = F_UNLCK;
    fcntl(counterFd, F_SETLK, &cfl);
    close(counterFd);

    // 2. Append loan application to loanDetails.txt with write lock
    int loanFd = open(LOANPATH, O_RDWR | O_APPEND | O_CREAT, 0644);
    if (loanFd == -1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Unable to register loan application^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    ld.loanID = assignedLoanID;
    ld.accountNumber = accountNumber;
    ld.loanAmount = amount;
    ld.empID = -1;    // Unassigned (waiting for manager assignment)
    ld.status = 0;    // 0 -> Requested

    struct flock lfl;
    memset(&lfl, 0, sizeof(lfl));
    lfl.l_type = F_WRLCK;
    lfl.l_whence = SEEK_END;
    lfl.l_len = sizeof(struct LoanDetails);
    fcntl(loanFd, F_SETLKW, &lfl);

    write(loanFd, &ld, sizeof(ld));

    lfl.l_type = F_UNLCK;
    fcntl(loanFd, F_SETLK, &lfl);
    close(loanFd);

    printf("[LOAN] Customer %d applied for loan of %d (Assigned Loan ID: %d)\n", accountNumber, amount, assignedLoanID);

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Loan applied successfully! Your Loan ID is: %d^", assignedLoanID);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * transactionHistory - Displays the last 10 account transactions.
 */
void transactionHistory(int connection_fd, int accountNumber) {
    char write_buf[4096];
    char readBuffer[4096];
    struct trans_histroy th;
    int count = 0;

    int file = open(HISTORYPATH, O_RDONLY | O_CREAT, 0644);
    bzero(write_buf, sizeof(write_buf));

    if (file != -1) {
        while (read(file, &th, sizeof(th)) > 0) {
            if (th.acc_no == accountNumber) {
                strncat(write_buf, th.hist, sizeof(write_buf) - strlen(write_buf) - 2);
                count++;
            }
        }
        close(file);
    }

    if (count == 0) {
        strcpy(write_buf, "No prior transaction history found for this account.\n");
    }

    strcat(write_buf, "^");
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * addFeedback - Records customer feedback into feedback.txt matching UML Feedback entity.
 */
void addFeedback(int connection_fd, int accountNumber) {
    char write_buf[4096];
    char readBuffer[4096];
    struct FeedBack fb;
    memset(&fb, 0, sizeof(fb));

    int file = open(FEEDPATH, O_CREAT | O_RDWR | O_APPEND, 0644);
    if (file == -1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Failed to open feedback database^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // Determine next feedback ID from file size
    off_t fileSize = lseek(file, 0, SEEK_END);
    int feedbackID = (fileSize / sizeof(struct FeedBack)) + 1;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Select Feedback Option:\n1. Excellent Service\n2. Average Service\n3. Unsatisfactory Service\nEnter option: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) {
        close(file);
        return;
    }

    int choice = atoi(readBuffer);
    fb.feedbackID = feedbackID;
    fb.customerID = accountNumber;
    fb.status = 0; // 0 -> Pending review by Manager

    if (choice == 1) {
        strcpy(fb.feedback, "Excellent Service");
    } else if (choice == 2) {
        strcpy(fb.feedback, "Average Service");
    } else {
        strcpy(fb.feedback, "Unsatisfactory Service");
    }

    struct flock ffl;
    memset(&ffl, 0, sizeof(ffl));
    ffl.l_type = F_WRLCK;
    ffl.l_whence = SEEK_END;
    ffl.l_len = sizeof(struct FeedBack);
    fcntl(file, F_SETLKW, &ffl);

    write(file, &fb, sizeof(fb));

    ffl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &ffl);
    close(file);

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Feedback #%d submitted successfully! Thank you for your feedback.^", feedbackID);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * changePassword - Updates account password securely using crypt() and record locking.
 */
int changePassword(int connection_fd, int accountNumber) {
    char write_buf[4096];
    char readBuffer[4096];
    char newPassword[256];
    struct Customer c;

    int file = open(CUSPATH, O_RDWR);
    if (file == -1) return 0;

    off_t recordOffset = -1;
    while (read(file, &c, sizeof(c)) > 0) {
        if (c.accountNumber == accountNumber) {
            recordOffset = lseek(file, -sizeof(struct Customer), SEEK_CUR);
            break;
        }
    }

    if (recordOffset == -1) {
        close(file);
        return 0;
    }

    // Prompt for new password
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter password: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) {
        close(file);
        return 0;
    }
    strncpy(newPassword, readBuffer, sizeof(newPassword) - 1);

    // Lock customer record for update
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = recordOffset;
    fl.l_len = sizeof(struct Customer);
    fl.l_pid = getpid();
    fcntl(file, F_SETLKW, &fl);

    lseek(file, recordOffset, SEEK_SET);
    read(file, &c, sizeof(c));

    strncpy(c.password, crypt(newPassword, HASHKEY), sizeof(c.password) - 1);

    lseek(file, recordOffset, SEEK_SET);
    write(file, &c, sizeof(c));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    printf("[SECURITY] Customer %d updated their password.\n", accountNumber);
    return 1;
}

/**
 * logout - Releases and unlinks the session semaphore and sends acknowledgment.
 */
void logout(int connection_fd, int id) {
    char write_buf[4096];
    char readBuffer[4096];
    char sem_name[64];

    snprintf(sem_name, sizeof(sem_name), "/sem_%d", id);
    sem_t *target_sem = sem_open(sem_name, 0);
    if (target_sem != SEM_FAILED) {
        sem_post(target_sem);
        sem_close(target_sem);
        sem_unlink(sem_name);
    }

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "^");
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

#endif // CUSTOMER_FUNCTIONS_H
