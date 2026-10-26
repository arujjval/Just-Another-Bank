#ifndef MANAGER_FUNCTIONS_H
#define MANAGER_FUNCTIONS_H

/*
 * =====================================================================================
 *  CS-513 System Software - Banking Management System
 *  Bank Manager Subsystem Controller
 * =====================================================================================
 *  System Software Concepts Implemented:
 *  1. Administrative Oversight & State Management:
 *     - Account lifecycle controls (Activate / Deactivate) with exclusive record locking.
 *     - Loan workflow pipeline dispatching: assigns unallocated loan requests to
 *       specific employees for review.
 *  2. Concurrency Control:
 *     - Record-level write locking (F_WRLCK) via fcntl prevents inconsistent states
 *       during account status changes and loan delegations.
 *  3. Feedback Review System:
 *     - Implements both get_all_feedback and review_feedback (from UML diagram) to
 *       mark customer feedback as resolved or reviewed.
 *  4. Single-Session Enforcement:
 *     - POSIX named semaphore (/sem_<mngID>) guarantees one concurrent session.
 * =====================================================================================
 */

void managerMenu(int connection_fd);
int  loginManager(int connection_fd, int mngID, char *password);
void changeStatus(int connection_fd);
void assignLoanApplication(int connection_fd);
void readFeedBack(int connection_fd);
void reviewFeedback(int connection_fd);
int  changeMNGPassword(int connection_fd, int mngID);

/**
 * managerMenu - Interactive command dispatcher for authenticated managers.
 */
void managerMenu(int connection_fd) {
    int mngID;
    char password[256];
    char write_buf[4096];
    char readBuffer[4096];
    int readBytes, writeBytes;

start:
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "\nEnter Manager ID: ");
    if (write(connection_fd, write_buf, sizeof(write_buf)) <= 0) return;

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    mngID = atoi(readBuffer);

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter password: ");
    if (write(connection_fd, write_buf, sizeof(write_buf)) <= 0) return;

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    strncpy(password, readBuffer, sizeof(password) - 1);

    int loginResult = loginManager(connection_fd, mngID, password);
    if (loginResult == 1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nLogin Successfully^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));

        while (1) {
            bzero(write_buf, sizeof(write_buf));
            strcpy(write_buf, MNGMENU);
            if (write(connection_fd, write_buf, sizeof(write_buf)) <= 0) {
                logout(connection_fd, mngID);
                return;
            }

            bzero(readBuffer, sizeof(readBuffer));
            if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) {
                logout(connection_fd, mngID);
                return;
            }
            int choice = atoi(readBuffer);

            switch (choice) {
                case 1:
                    // Activate / Deactivate customer accounts
                    changeStatus(connection_fd);
                    break;
                case 2:
                    // Assign loan applications to employees
                    assignLoanApplication(connection_fd);
                    break;
                case 3:
                    // Review customer feedback
                    readFeedBack(connection_fd);
                    break;
                case 4: {
                    int resp = changeMNGPassword(connection_fd, mngID);
                    bzero(write_buf, sizeof(write_buf));
                    bzero(readBuffer, sizeof(readBuffer));
                    if (resp) {
                        strcpy(write_buf, "Password changed successfully! Please log in again.^");
                    } else {
                        strcpy(write_buf, "Unable to change password^");
                    }
                    write(connection_fd, write_buf, sizeof(write_buf));
                    read(connection_fd, readBuffer, sizeof(readBuffer));
                    logout(connection_fd, mngID);
                    goto start;
                }
                case 5:
                    printf("[MANAGER] Manager ID %d logged out.\n", mngID);
                    logout(connection_fd, mngID);
                    return;
                case 6:
                    printf("[MANAGER] Manager ID %d requested exit.\n", mngID);
                    exitClient(connection_fd, mngID, NULL);
                    return;
                default:
                    bzero(write_buf, sizeof(write_buf));
                    strcpy(write_buf, "Invalid choice for manager section^");
                    write(connection_fd, write_buf, sizeof(write_buf));
                    read(connection_fd, readBuffer, sizeof(readBuffer));
                    break;
            }
        }
    } else if (loginResult == -2) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nManager is already logged in on another session!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        goto start;
    } else {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nInvalid Manager ID or Password^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        goto start;
    }
}

/**
 * loginManager - Authenticates credentials and locks named semaphore.
 */
int loginManager(int connection_fd, int mngID, char *password) {
    struct Employee mng;
    int file = open(EMPPATH, O_RDONLY);
    if (file == -1) return 0;

    int authenticated = 0;
    while (read(file, &mng, sizeof(mng)) > 0) {
        // Manager role is 0
        if (mng.empID == mngID &&
            strcmp(mng.password, crypt(password, HASHKEY)) == 0 &&
            mng.role == 0) {
            authenticated = 1;
            break;
        }
    }
    close(file);

    if (!authenticated) return 0;

    sema = initializeSemaphore(mngID);
    setupSignalHandlers();

    if (sem_trywait(sema) == -1) {
        if (errno == EAGAIN) {
            printf("[AUTH] Manager %d already logged in elsewhere.\n", mngID);
            return -2;
        }
        perror("sem_trywait error");
        return 0;
    }

    printf("[AUTH] Manager %d authenticated.\n", mngID);
    return 1;
}

/**
 * changeStatus - Modifies customer account active status with record-level write lock.
 */
void changeStatus(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    struct Customer cs;
    int accNo, choice;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Customer Account Number: ");
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
        strcpy(write_buf, "Account number not found or deleted!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // Acquire write lock before modifying state
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

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf),
             "Account: %d | Current Status: %s\n1. Deactivate Account\n2. Activate Account\nEnter option: ",
             cs.accountNumber, (cs.activeStatus == 1 ? "Active" : "Deactivated"));
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) > 0) {
        choice = atoi(readBuffer);
        if (choice == 1) {
            cs.activeStatus = 0; // Deactivate
        } else if (choice == 2) {
            cs.activeStatus = 1; // Activate
        }

        lseek(file, offset, SEEK_SET);
        write(file, &cs, sizeof(cs));
    }

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Account #%d status updated successfully!^", accNo);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * assignLoanApplication - Assigns pending loan applications to bank employees.
 */
void assignLoanApplication(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    char lineBuffer[256];
    struct LoanDetails ld;
    int unassignedCount = 0;

    int file = open(LOANPATH, O_RDWR);
    if (file == -1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Unable to open loan database^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // Step 1: Display all unassigned loan requests (empID == -1 and status == 0)
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "--- Unassigned Loan Applications ---\n");

    while (read(file, &ld, sizeof(ld)) > 0) {
        if (ld.empID == -1 && ld.status == 0) {
            snprintf(lineBuffer, sizeof(lineBuffer),
                     "Loan ID: %d | Account: %d | Amount: %d\n",
                     ld.loanID, ld.accountNumber, ld.loanAmount);
            strncat(write_buf, lineBuffer, sizeof(write_buf) - strlen(write_buf) - 2);
            unassignedCount++;
        }
    }

    if (unassignedCount == 0) {
        close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "No unassigned loan applications currently pending.^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    strcat(write_buf, "Enter Loan ID to assign: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) {
        close(file);
        return;
    }
    int targetLoanID = atoi(readBuffer);

    // Step 2: Locate target loan record and lock it
    lseek(file, 0, SEEK_SET);
    off_t loanOffset = -1;
    while (read(file, &ld, sizeof(ld)) > 0) {
        if (ld.loanID == targetLoanID && ld.empID == -1) {
            loanOffset = lseek(file, -sizeof(struct LoanDetails), SEEK_CUR);
            break;
        }
    }

    if (loanOffset == -1) {
        close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Invalid Loan ID or loan is already assigned!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = loanOffset;
    fl.l_len = sizeof(struct LoanDetails);
    fl.l_pid = getpid();
    fcntl(file, F_SETLKW, &fl);

    // Prompt for target employee ID
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Employee ID to assign this loan to: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) {
        fl.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl);
        close(file);
        return;
    }
    int targetEmpID = atoi(readBuffer);

    // Verify target employee exists and is an active employee (role == 1)
    int empFile = open(EMPPATH, O_RDONLY);
    int empFound = 0;
    if (empFile != -1) {
        struct Employee emp;
        while (read(empFile, &emp, sizeof(emp)) > 0) {
            if (emp.empID == targetEmpID && emp.role == 1) {
                empFound = 1;
                break;
            }
        }
        close(empFile);
    }

    if (!empFound) {
        fl.l_type = F_UNLCK; fcntl(file, F_SETLK, &fl);
        close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Error: Specified Employee ID does not exist or is not an active employee!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // Update loan record: assign employee and set status to 1 (Pending review)
    lseek(file, loanOffset, SEEK_SET);
    read(file, &ld, sizeof(ld));

    ld.empID = targetEmpID;
    ld.status = 1; // Pending employee review

    lseek(file, loanOffset, SEEK_SET);
    write(file, &ld, sizeof(ld));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    printf("[MANAGER] Loan #%d assigned to Employee ID %d.\n", targetLoanID, targetEmpID);

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Loan #%d successfully assigned to Employee #%d!^", targetLoanID, targetEmpID);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * readFeedBack - Lists all customer feedback and provides option to review/resolve items.
 * Fulfills both get_all_feedback and review_feedback from the UML Class Diagram.
 */
void readFeedBack(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    char itemBuffer[256];
    struct FeedBack fb;
    int totalFeedbacks = 0;

    int file = open(FEEDPATH, O_RDWR);
    bzero(write_buf, sizeof(write_buf));

    if (file != -1) {
        strcpy(write_buf, "\n--- Customer Feedback Registry ---\n");
        while (read(file, &fb, sizeof(fb)) > 0) {
            snprintf(itemBuffer, sizeof(itemBuffer),
                     "Feedback #%d | Customer #%d | Status: %s\n  Message: %s\n",
                     fb.feedbackID, fb.customerID,
                     (fb.status == 1 ? "Reviewed" : "Pending"),
                     fb.feedback);
            strncat(write_buf, itemBuffer, sizeof(write_buf) - strlen(write_buf) - 2);
            totalFeedbacks++;
        }
    }

    if (totalFeedbacks == 0) {
        if (file != -1) close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "No customer feedback recorded yet.^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    strcat(write_buf, "\nEnter Feedback # to mark as Reviewed (or 0 to return): ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) {
        close(file);
        return;
    }
    int targetFbID = atoi(readBuffer);

    if (targetFbID <= 0) {
        close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // Locate feedback record and mark as Reviewed (review_feedback)
    lseek(file, 0, SEEK_SET);
    off_t fbOffset = -1;
    while (read(file, &fb, sizeof(fb)) > 0) {
        if (fb.feedbackID == targetFbID) {
            fbOffset = lseek(file, -sizeof(struct FeedBack), SEEK_CUR);
            break;
        }
    }

    if (fbOffset != -1) {
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = fbOffset;
        fl.l_len = sizeof(struct FeedBack);
        fl.l_pid = getpid();
        fcntl(file, F_SETLKW, &fl);

        lseek(file, fbOffset, SEEK_SET);
        read(file, &fb, sizeof(fb));
        fb.status = 1; // Reviewed

        lseek(file, fbOffset, SEEK_SET);
        write(file, &fb, sizeof(fb));

        fl.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl);

        bzero(write_buf, sizeof(write_buf));
        snprintf(write_buf, sizeof(write_buf), "Feedback #%d marked as Reviewed!^", targetFbID);
    } else {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Feedback ID not found!^");
    }

    close(file);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * changeMNGPassword - Updates manager password.
 */
int changeMNGPassword(int connection_fd, int mngID) {
    char write_buf[4096];
    char readBuffer[4096];
    char newPassword[256];
    struct Employee m;

    int file = open(EMPPATH, O_RDWR);
    if (file == -1) return 0;

    off_t offset = -1;
    while (read(file, &m, sizeof(m)) > 0) {
        if (m.empID == mngID && m.role == 0) {
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
    read(file, &m, sizeof(m));

    strncpy(m.password, crypt(newPassword, HASHKEY), sizeof(m.password) - 1);

    lseek(file, offset, SEEK_SET);
    write(file, &m, sizeof(m));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    return 1;
}

#endif // MANAGER_FUNCTIONS_H
