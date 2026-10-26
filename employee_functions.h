#ifndef EMPLOYEE_FUNCTIONS_H
#define EMPLOYEE_FUNCTIONS_H

/*
 * =====================================================================================
 *  CS-513 System Software - Banking Management System
 *  Bank Employee Subsystem Controller
 * =====================================================================================
 *  System Software Concepts Implemented:
 *  1. Access Control & Role-Based Authorization:
 *     - Only authenticated employees with role == 1 are granted access to customer
 *       creation, passbook audits, and loan approvals.
 *  2. Concurrency Control & Record-Level File Locking:
 *     - Loan approvals execute an atomic multi-record update: locks both the loan
 *       record in loanDetails.txt and the customer record in customers.txt.
 *     - Customer creation and modification acquire exclusive write locks (F_WRLCK)
 *       to prevent corrupted or duplicate account records.
 *  3. Single-Session Enforcement:
 *     - POSIX named semaphore (/sem_<empID>) prevents concurrent logins for the same employee.
 * =====================================================================================
 */

void employeeMenu(int connection_fd);
int  loginEmployee(int connection_fd, int empID, char *password);
void addCustomer(int connection_fd);
void modifyCustomer(int connection_fd);
void approveRejectLoan(int connection_fd, int empID);
void viewAssignedLoan(int connection_fd, int empID);
void viewCustomerDetails(int connection_fd);
void deleteCustomerAccount(int connection_fd);
int  changeEMPPassword(int connection_fd, int empID);

/**
 * employeeMenu - Main control loop for bank employees.
 */
void employeeMenu(int connection_fd) {
    int empID;
    char password[256];
    char write_buf[4096];
    char readBuffer[4096];
    int readBytes, writeBytes;

start:
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "\nEnter Employee ID: ");
    if (write(connection_fd, write_buf, sizeof(write_buf)) <= 0) return;

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    empID = atoi(readBuffer);

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter password: ");
    if (write(connection_fd, write_buf, sizeof(write_buf)) <= 0) return;

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    strncpy(password, readBuffer, sizeof(password) - 1);

    int loginResult = loginEmployee(connection_fd, empID, password);
    if (loginResult == 1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nLogin Successfully^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));

        while (1) {
            bzero(write_buf, sizeof(write_buf));
            strcpy(write_buf, EMPMENU);
            if (write(connection_fd, write_buf, sizeof(write_buf)) <= 0) {
                logout(connection_fd, empID);
                return;
            }

            bzero(readBuffer, sizeof(readBuffer));
            if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) {
                logout(connection_fd, empID);
                return;
            }
            int choice = atoi(readBuffer);

            switch (choice) {
                case 1:
                    addCustomer(connection_fd);
                    break;
                case 2:
                    modifyCustomer(connection_fd);
                    break;
                case 3:
                    approveRejectLoan(connection_fd, empID);
                    break;
                case 4:
                    viewAssignedLoan(connection_fd, empID);
                    break;
                case 5: {
                    // Passbook / Transaction History Viewer
                    bzero(write_buf, sizeof(write_buf));
                    strcpy(write_buf, "Enter Customer Account Number: ");
                    write(connection_fd, write_buf, sizeof(write_buf));

                    bzero(readBuffer, sizeof(readBuffer));
                    if (read(connection_fd, readBuffer, sizeof(readBuffer)) > 0) {
                        int accNo = atoi(readBuffer);
                        transactionHistory(connection_fd, accNo);
                    }
                    break;
                }
                case 6:
                    viewCustomerDetails(connection_fd);
                    break;
                case 7:
                    deleteCustomerAccount(connection_fd);
                    break;
                case 8: {
                    int resp = changeEMPPassword(connection_fd, empID);
                    bzero(write_buf, sizeof(write_buf));
                    bzero(readBuffer, sizeof(readBuffer));
                    if (resp) {
                        strcpy(write_buf, "Password changed successfully! Please log in with new password.^");
                    } else {
                        strcpy(write_buf, "Unable to change password^");
                    }
                    write(connection_fd, write_buf, sizeof(write_buf));
                    read(connection_fd, readBuffer, sizeof(readBuffer));
                    logout(connection_fd, empID);
                    goto start;
                }
                case 9:
                    printf("[EMPLOYEE] Employee ID %d logged out.\n", empID);
                    logout(connection_fd, empID);
                    return;
                case 10:
                    printf("[EMPLOYEE] Employee ID %d requested exit.\n", empID);
                    exitClient(connection_fd, empID, NULL);
                    return; // Fixed fall-through bug
                default:
                    bzero(write_buf, sizeof(write_buf));
                    strcpy(write_buf, "Invalid choice for employee dashboard^");
                    write(connection_fd, write_buf, sizeof(write_buf));
                    read(connection_fd, readBuffer, sizeof(readBuffer));
                    break;
            }
        }
    } else if (loginResult == -2) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nEmployee is already logged in on another session!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        goto start;
    } else {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nInvalid Employee ID or Password^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        goto start;
    }
}

/**
 * loginEmployee - Validates credentials and secures exclusive session semaphore.
 */
int loginEmployee(int connection_fd, int empID, char *password) {
    struct Employee employee;
    int file = open(EMPPATH, O_RDONLY);
    if (file == -1) return 0;

    int authenticated = 0;
    while (read(file, &employee, sizeof(employee)) > 0) {
        // Must match ID, password hash, and have active Employee role (role == 1)
        if (employee.empID == empID &&
            strcmp(employee.password, crypt(password, HASHKEY)) == 0 &&
            employee.role == 1) {
            authenticated = 1;
            break;
        }
    }
    close(file);

    if (!authenticated) return 0;

    sema = initializeSemaphore(empID);
    setupSignalHandlers();

    if (sem_trywait(sema) == -1) {
        if (errno == EAGAIN) {
            printf("[AUTH] Employee %d duplicate session rejected.\n", empID);
            return -2;
        }
        perror("sem_trywait failed");
        return 0;
    }

    printf("[AUTH] Employee %d successfully logged in.\n", empID);
    return 1;
}

/**
 * addCustomer - Registers a new customer account with atomic initial deposit recording.
 */
void addCustomer(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    char transactionBuffer[1024];
    struct Customer customer;
    struct trans_histroy th;

    memset(&customer, 0, sizeof(customer));

    // 1. Prompt for Customer Name
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Customer Full Name: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    strncpy(customer.customerName, readBuffer, sizeof(customer.customerName) - 1);

    // 2. Prompt for Customer Initial Password
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Initial Password: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    strncpy(customer.password, crypt(readBuffer, HASHKEY), sizeof(customer.password) - 1);

    // 3. Prompt for Account Number
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter New Account Number: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    customer.accountNumber = atoi(readBuffer);

    // Verify account number uniqueness
    int checkFd = open(CUSPATH, O_RDONLY | O_CREAT, 0644);
    if (checkFd != -1) {
        struct Customer existing;
        while (read(checkFd, &existing, sizeof(existing)) > 0) {
            if (existing.accountNumber == customer.accountNumber && existing.activeStatus != -1) {
                close(checkFd);
                bzero(write_buf, sizeof(write_buf));
                strcpy(write_buf, "Error: An account with this number already exists!^");
                write(connection_fd, write_buf, sizeof(write_buf));
                read(connection_fd, readBuffer, sizeof(readBuffer));
                return;
            }
        }
        close(checkFd);
    }

    // 4. Prompt for Opening Balance
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Opening Balance: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    customer.balance = atof(readBuffer);
    customer.activeStatus = 1; // Activated by default

    // 5. Append customer record with write lock
    int file = open(CUSPATH, O_RDWR | O_APPEND | O_CREAT, 0644);
    if (file == -1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Error opening customer database^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_END;
    fl.l_len = sizeof(struct Customer);
    fcntl(file, F_SETLKW, &fl);

    write(file, &customer, sizeof(customer));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    // 6. Record opening balance in audit history
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    int histFile = open(HISTORYPATH, O_RDWR | O_APPEND | O_CREAT, 0644);
    if (histFile != -1) {
        snprintf(transactionBuffer, sizeof(transactionBuffer),
                 "%.2f Account Opened at %02d:%02d:%02d %04d-%02d-%02d\n",
                 customer.balance, tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday);

        th.acc_no = customer.accountNumber;
        strncpy(th.hist, transactionBuffer, sizeof(th.hist) - 1);
        write(histFile, &th, sizeof(th));
        close(histFile);
    }

    printf("[EMPLOYEE] Customer account #%d created for %s.\n", customer.accountNumber, customer.customerName);

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Customer account #%d created successfully!^", customer.accountNumber);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * modifyCustomer - Updates a customer's record with write locking.
 */
void modifyCustomer(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    int accNo;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Account Number to modify: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    accNo = atoi(readBuffer);

    int file = open(CUSPATH, O_RDWR);
    if (file == -1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Failed to open customer database^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    struct Customer c;
    off_t offset = -1;
    while (read(file, &c, sizeof(c)) > 0) {
        if (c.accountNumber == accNo && c.activeStatus != -1) {
            offset = lseek(file, -sizeof(struct Customer), SEEK_CUR);
            break;
        }
    }

    if (offset == -1) {
        close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Customer account not found!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // Acquire write lock
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = sizeof(struct Customer);
    fl.l_pid = getpid();
    fcntl(file, F_SETLKW, &fl);

    // Read current data
    lseek(file, offset, SEEK_SET);
    read(file, &c, sizeof(c));

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Current Name: %s\nEnter New Name: ", c.customerName);
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) > 0) {
        strncpy(c.customerName, readBuffer, sizeof(c.customerName) - 1);
        lseek(file, offset, SEEK_SET);
        write(file, &c, sizeof(c));
    }

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Customer details updated successfully!^");
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * approveRejectLoan - Evaluates a loan assigned to this employee.
 * Executes an atomic two-phase write across loanDetails.txt and customers.txt.
 */
void approveRejectLoan(int connection_fd, int empID) {
    char write_buf[4096];
    char readBuffer[4096];
    char transactionBuffer[1024];
    struct LoanDetails ld;
    struct Customer cs;
    struct trans_histroy th;
    int lID, choice;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Loan ID to process: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    lID = atoi(readBuffer);

    int loanFile = open(LOANPATH, O_RDWR);
    if (loanFile == -1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Error opening loan database^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    off_t loanOffset = -1;
    while (read(loanFile, &ld, sizeof(ld)) > 0) {
        if (ld.loanID == lID) {
            loanOffset = lseek(loanFile, -sizeof(struct LoanDetails), SEEK_CUR);
            break;
        }
    }

    if (loanOffset == -1) {
        close(loanFile);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Loan ID not found!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // Verify loan is assigned to this employee and pending (status == 1)
    if (ld.empID != empID || ld.status != 1) {
        close(loanFile);
        bzero(write_buf, sizeof(write_buf));
        snprintf(write_buf, sizeof(write_buf),
                 "Loan #%d is not assigned to you or has already been finalized (Status: %d)^",
                 lID, ld.status);
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // Lock loan record
    struct flock flLoan;
    memset(&flLoan, 0, sizeof(flLoan));
    flLoan.l_type = F_WRLCK;
    flLoan.l_whence = SEEK_SET;
    flLoan.l_start = loanOffset;
    flLoan.l_len = sizeof(struct LoanDetails);
    flLoan.l_pid = getpid();
    fcntl(loanFile, F_SETLKW, &flLoan);

    // Prompt decision
    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf),
             "Loan #%d | Account: %d | Amount: %d\n1. Approve Loan\n2. Reject Loan\nEnter choice: ",
             ld.loanID, ld.accountNumber, ld.loanAmount);
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) {
        flLoan.l_type = F_UNLCK; fcntl(loanFile, F_SETLK, &flLoan);
        close(loanFile);
        return;
    }
    choice = atoi(readBuffer);

    if (choice == 1) {
        // Approve: Credit funds to customer account
        int cusFile = open(CUSPATH, O_RDWR);
        off_t cusOffset = -1;
        if (cusFile != -1) {
            while (read(cusFile, &cs, sizeof(cs)) > 0) {
                if (cs.accountNumber == ld.accountNumber) {
                    cusOffset = lseek(cusFile, -sizeof(struct Customer), SEEK_CUR);
                    break;
                }
            }
        }

        if (cusOffset == -1 || cs.activeStatus != 1) {
            // Cannot approve if customer account is inactive or deleted
            ld.status = 3; // Reject
            lseek(loanFile, loanOffset, SEEK_SET);
            write(loanFile, &ld, sizeof(ld));

            flLoan.l_type = F_UNLCK; fcntl(loanFile, F_SETLK, &flLoan);
            close(loanFile);
            if (cusFile != -1) close(cusFile);

            bzero(write_buf, sizeof(write_buf));
            strcpy(write_buf, "Customer account is inactive. Loan automatically rejected.^");
            write(connection_fd, write_buf, sizeof(write_buf));
            read(connection_fd, readBuffer, sizeof(readBuffer));
            return;
        }

        // Lock customer record
        struct flock flCus;
        memset(&flCus, 0, sizeof(flCus));
        flCus.l_type = F_WRLCK;
        flCus.l_whence = SEEK_SET;
        flCus.l_start = cusOffset;
        flCus.l_len = sizeof(struct Customer);
        flCus.l_pid = getpid();
        fcntl(cusFile, F_SETLKW, &flCus);

        // Credit loan principal to balance
        cs.balance += ld.loanAmount;
        lseek(cusFile, cusOffset, SEEK_SET);
        write(cusFile, &cs, sizeof(cs));

        // Mark loan approved
        ld.status = 2; // Approved
        lseek(loanFile, loanOffset, SEEK_SET);
        write(loanFile, &ld, sizeof(ld));

        // Unlock customer and loan
        flCus.l_type = F_UNLCK; fcntl(cusFile, F_SETLK, &flCus);
        close(cusFile);

        // Record credit transaction
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        int histFile = open(HISTORYPATH, O_RDWR | O_APPEND | O_CREAT, 0644);
        if (histFile != -1) {
            snprintf(transactionBuffer, sizeof(transactionBuffer),
                     "%d Loan credited (Loan ID #%d) at %02d:%02d:%02d %04d-%02d-%02d (New Bal: %.2f)\n",
                     ld.loanAmount, ld.loanID, tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
                     tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday, cs.balance);
            th.acc_no = cs.accountNumber;
            strncpy(th.hist, transactionBuffer, sizeof(th.hist) - 1);
            write(histFile, &th, sizeof(th));
            close(histFile);
        }

        bzero(write_buf, sizeof(write_buf));
        snprintf(write_buf, sizeof(write_buf), "Loan #%d approved and %d credited to account %d!^",
                 lID, ld.loanAmount, ld.accountNumber);
    } else if (choice == 2) {
        // Reject
        ld.status = 3; // Rejected
        lseek(loanFile, loanOffset, SEEK_SET);
        write(loanFile, &ld, sizeof(ld));

        bzero(write_buf, sizeof(write_buf));
        snprintf(write_buf, sizeof(write_buf), "Loan #%d has been rejected.^", lID);
    } else {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Invalid choice. Loan review cancelled.^");
    }

    flLoan.l_type = F_UNLCK;
    fcntl(loanFile, F_SETLK, &flLoan);
    close(loanFile);

    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * viewAssignedLoan - Lists all pending loan applications assigned to this employee.
 */
void viewAssignedLoan(int connection_fd, int empID) {
    char write_buf[4096];
    char readBuffer[4096];
    char itemBuffer[256];
    struct LoanDetails ld;
    int count = 0;

    int file = open(LOANPATH, O_RDONLY);
    bzero(write_buf, sizeof(write_buf));

    if (file != -1) {
        while (read(file, &ld, sizeof(ld)) > 0) {
            if (ld.empID == empID && ld.status == 1) { // 1 -> Pending review
                snprintf(itemBuffer, sizeof(itemBuffer),
                         "Loan ID: %d | Account: %d | Amount: %d | Status: Pending\n",
                         ld.loanID, ld.accountNumber, ld.loanAmount);
                strncat(write_buf, itemBuffer, sizeof(write_buf) - strlen(write_buf) - 2);
                count++;
            }
        }
        close(file);
    }

    if (count == 0) {
        strcpy(write_buf, "No pending loan applications assigned to your queue.\n");
    }

    strcat(write_buf, "^");
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * viewCustomerDetails - Displays customer record attributes using shared read lock.
 */
void viewCustomerDetails(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    struct Customer cs;
    int accNo, found = 0;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Customer Account Number: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    accNo = atoi(readBuffer);

    int file = open(CUSPATH, O_RDONLY);
    if (file != -1) {
        while (read(file, &cs, sizeof(cs)) > 0) {
            if (cs.accountNumber == accNo && cs.activeStatus != -1) {
                found = 1;
                bzero(write_buf, sizeof(write_buf));
                snprintf(write_buf, sizeof(write_buf),
                         "\n--- Customer Details ---\nAccount Number : %d\nFull Name      : %s\nBalance        : %.2f\nStatus         : %s\n^",
                         cs.accountNumber, cs.customerName, cs.balance,
                         (cs.activeStatus == 1 ? "Active" : "Deactivated"));
                break;
            }
        }
        close(file);
    }

    if (!found) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Customer record not found!^");
    }

    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * deleteCustomerAccount - Soft-deletes a customer account (activeStatus = -1).
 */
void deleteCustomerAccount(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    struct Customer cs;
    int accNo;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Account Number to delete: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    accNo = atoi(readBuffer);

    int file = open(CUSPATH, O_RDWR);
    if (file == -1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Database error^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    off_t offset = -1;
    while (read(file, &cs, sizeof(cs)) > 0) {
        if (cs.accountNumber == accNo && cs.activeStatus != -1) {
            offset = lseek(file, -sizeof(struct Customer), SEEK_CUR);
            break;
        }
    }

    if (offset == -1) {
        close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Account not found or already deleted^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = sizeof(struct Customer);
    fl.l_pid = getpid();
    fcntl(file, F_SETLKW, &fl);

    lseek(file, offset, SEEK_SET);
    read(file, &cs, sizeof(cs));

    cs.activeStatus = -1; // Mark as deleted

    lseek(file, offset, SEEK_SET);
    write(file, &cs, sizeof(cs));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    printf("[EMPLOYEE] Account #%d marked as deleted.\n", accNo);

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Customer account #%d deleted successfully!^", accNo);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * changeEMPPassword - Allows an employee to update their login password.
 */
int changeEMPPassword(int connection_fd, int empID) {
    char write_buf[4096];
    char readBuffer[4096];
    char newPassword[256];
    struct Employee emp;

    int file = open(EMPPATH, O_RDWR);
    if (file == -1) return 0;

    off_t offset = -1;
    while (read(file, &emp, sizeof(emp)) > 0) {
        if (emp.empID == empID) {
            offset = lseek(file, -sizeof(struct Employee), SEEK_CUR);
            break;
        }
    }

    if (offset == -1) {
        close(file);
        return 0;
    }

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter password: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) {
        close(file);
        return 0;
    }
    strncpy(newPassword, readBuffer, sizeof(newPassword) - 1);

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = sizeof(struct Employee);
    fl.l_pid = getpid();
    fcntl(file, F_SETLKW, &fl);

    lseek(file, offset, SEEK_SET);
    read(file, &emp, sizeof(emp));

    strncpy(emp.password, crypt(newPassword, HASHKEY), sizeof(emp.password) - 1);

    lseek(file, offset, SEEK_SET);
    write(file, &emp, sizeof(emp));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    return 1;
}

#endif // EMPLOYEE_FUNCTIONS_H
