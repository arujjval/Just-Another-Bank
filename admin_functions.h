#ifndef ADMIN_FUNCTIONS_H
#define ADMIN_FUNCTIONS_H

/*
 * =====================================================================================
 *  CS-513 System Software - Banking Management System
 *  System Administrator Subsystem Controller
 * =====================================================================================
 *  System Software Concepts Implemented:
 *  1. Root Governance & Persistent Credential Storage:
 *     - Admin accounts are stored persistently in admin.txt (struct Admin).
 *     - If no admin record exists, a default administrator is initialized with
 *       SHA-512 crypt() password encryption.
 *  2. Single-Session Enforcement:
 *     - POSIX named semaphore (/sem_admin_session) guarantees that only one administrator
 *       session can be active at any given moment.
 *  3. Role Management & Access Control:
 *     - Administrator can promote employees to managers (role = 0) or demote
 *       managers to employees (role = 1).
 *  4. Concurrency & ACID Integrity:
 *     - Exclusive file record locking (F_WRLCK) via fcntl prevents dirty reads
 *       and conflicting updates during employee registration, modifications, and deletions.
 *  5. Complete UML Method Support:
 *     - Implements add_employee, delete_employee, delete_customer, manage_user_roles,
 *       get_employee_details, get_customer_details, and change_admin_password.
 * =====================================================================================
 */

#define ADMIN_SEM_NAME "/sem_admin_session"

int  initAdminAccount();
int  loginAdmin(int connection_fd, char *password);
int  addEmployee(int connection_fd);
void modifyCE(int connection_fd, int modifyChoice);
void manageRole(int connection_fd);
void viewAdminCustomerDetails(int connection_fd);
void viewAdminEmployeeDetails(int connection_fd);
void adminDeleteCustomer(int connection_fd);
void adminDeleteEmployee(int connection_fd);
int  adminChangePassword(int connection_fd);
void adminLogout(int connection_fd);

/**
 * initAdminAccount - Ensures an admin record exists on disk in admin.txt.
 * Initializes default credentials (ID: 1, User: admin, Pass: admin@123) if missing.
 */
int initAdminAccount() {
    int file = open(ADMINPATH, O_RDWR | O_CREAT, 0644);
    if (file == -1) return -1;

    struct Admin admin;
    int bytes = read(file, &admin, sizeof(admin));
    if (bytes <= 0) {
        // Initialize default admin
        memset(&admin, 0, sizeof(admin));
        admin.adminID = 1;
        strncpy(admin.firstName, "System", sizeof(admin.firstName) - 1);
        strncpy(admin.lastName, "Admin", sizeof(admin.lastName) - 1);
        strncpy(admin.password, crypt("admin@123", HASHKEY), sizeof(admin.password) - 1);

        lseek(file, 0, SEEK_SET);
        write(file, &admin, sizeof(admin));
        printf("[ADMIN] Default admin record initialized in %s.\n", ADMINPATH);
    }
    close(file);
    return 0;
}

/**
 * loginAdmin - Authenticates the administrator and acquires the exclusive session semaphore.
 */
int loginAdmin(int connection_fd, char *password) {
    initAdminAccount();

    struct Admin admin;
    int file = open(ADMINPATH, O_RDONLY);
    if (file == -1) return 0;

    int authenticated = 0;
    if (read(file, &admin, sizeof(admin)) > 0) {
        if (strcmp(admin.password, crypt(password, HASHKEY)) == 0) {
            authenticated = 1;
        }
    }
    close(file);

    if (!authenticated) return 0;

    // Enforce single active session for administrator
    sema = initializeNamedSemaphore(ADMIN_SEM_NAME);
    setupSignalHandlers();

    if (sem_trywait(sema) == -1) {
        if (errno == EAGAIN) {
            printf("[AUTH] Administrator session rejected: another admin session is active.\n");
            return -2;
        }
        perror("sem_trywait failed on admin semaphore");
        return 0;
    }

    printf("[AUTH] Administrator successfully authenticated.\n");
    return 1;
}

/**
 * adminMenu - Primary dispatch loop for administrative operations.
 */
void adminMenu(int connection_fd) {
    char password[256];
    char write_buf[4096];
    char readBuffer[4096];
    int choice;

start:
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "\nEnter password: ");
    if (write(connection_fd, write_buf, sizeof(write_buf)) <= 0) return;

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    strncpy(password, readBuffer, sizeof(password) - 1);

    int loginResult = loginAdmin(connection_fd, password);
    if (loginResult == 1) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nLogin Successfully^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));

        while (1) {
            bzero(write_buf, sizeof(write_buf));
            strcpy(write_buf, ADMINMENU);
            if (write(connection_fd, write_buf, sizeof(write_buf)) <= 0) {
                adminLogout(connection_fd);
                return;
            }

            bzero(readBuffer, sizeof(readBuffer));
            if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) {
                adminLogout(connection_fd);
                return;
            }
            choice = atoi(readBuffer);

            switch (choice) {
                case 1:
                    // Register New Employee
                    addEmployee(connection_fd);
                    break;
                case 2: {
                    // Update Customer/Employee Info
                    bzero(write_buf, sizeof(write_buf));
                    strcpy(write_buf, "1. Modify Customer Details\n2. Modify Employee Details\nEnter option: ");
                    write(connection_fd, write_buf, sizeof(write_buf));

                    bzero(readBuffer, sizeof(readBuffer));
                    if (read(connection_fd, readBuffer, sizeof(readBuffer)) > 0) {
                        int modChoice = atoi(readBuffer);
                        modifyCE(connection_fd, modChoice);
                    }
                    break;
                }
                case 3:
                    // Manage Roles and Permissions (promote / demote)
                    manageRole(connection_fd);
                    break;
                case 4:
                    // View Customer Details
                    viewAdminCustomerDetails(connection_fd);
                    break;
                case 5:
                    // View Employee Details
                    viewAdminEmployeeDetails(connection_fd);
                    break;
                case 6:
                    // Delete Customer Account
                    adminDeleteCustomer(connection_fd);
                    break;
                case 7:
                    // Delete Employee Account
                    adminDeleteEmployee(connection_fd);
                    break;
                case 8: {
                    // Change Admin Password
                    int resp = adminChangePassword(connection_fd);
                    bzero(write_buf, sizeof(write_buf));
                    bzero(readBuffer, sizeof(readBuffer));
                    if (resp) {
                        strcpy(write_buf, "Admin password updated successfully! Please log in again.^");
                    } else {
                        strcpy(write_buf, "Failed to update admin password.^");
                    }
                    write(connection_fd, write_buf, sizeof(write_buf));
                    read(connection_fd, readBuffer, sizeof(readBuffer));
                    adminLogout(connection_fd);
                    goto start;
                }
                case 9:
                    // Logout
                    printf("[ADMIN] Administrator logged out.\n");
                    adminLogout(connection_fd);
                    return;
                case 10:
                    // Exit
                    printf("[ADMIN] Administrator requested process exit.\n");
                    exitClient(connection_fd, 0, ADMIN_SEM_NAME);
                    return;
                default:
                    bzero(write_buf, sizeof(write_buf));
                    strcpy(write_buf, "Invalid choice for admin console^");
                    write(connection_fd, write_buf, sizeof(write_buf));
                    read(connection_fd, readBuffer, sizeof(readBuffer));
                    break;
            }
        }
    } else if (loginResult == -2) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nAdministrator is already logged in on another session!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        goto start;
    } else {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "\nInvalid Admin Password^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        goto start;
    }
}

/**
 * addEmployee - Registers a new bank employee or manager record with write locking.
 */
int addEmployee(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    struct Employee emp;
    memset(&emp, 0, sizeof(emp));

    // Employee ID
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter New Employee ID: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return 0;
    emp.empID = atoi(readBuffer);

    // Verify employee ID uniqueness
    int checkFile = open(EMPPATH, O_RDONLY | O_CREAT, 0644);
    if (checkFile != -1) {
        struct Employee existing;
        while (read(checkFile, &existing, sizeof(existing)) > 0) {
            if (existing.empID == emp.empID && existing.role != -1) {
                close(checkFile);
                bzero(write_buf, sizeof(write_buf));
                strcpy(write_buf, "Error: An employee with this ID already exists!^");
                write(connection_fd, write_buf, sizeof(write_buf));
                read(connection_fd, readBuffer, sizeof(readBuffer));
                return 0;
            }
        }
        close(checkFile);
    }

    // First Name
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter First Name: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return 0;
    strncpy(emp.firstName, readBuffer, sizeof(emp.firstName) - 1);

    // Last Name
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Last Name: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return 0;
    strncpy(emp.lastName, readBuffer, sizeof(emp.lastName) - 1);

    // Password
    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Initial Password: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return 0;
    strncpy(emp.password, crypt(readBuffer, HASHKEY), sizeof(emp.password) - 1);

    emp.role = 1; // Default role: Employee

    // Append to employees.txt with write lock
    int file = open(EMPPATH, O_RDWR | O_APPEND | O_CREAT, 0644);
    if (file == -1) return 0;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_END;
    fl.l_len = sizeof(struct Employee);
    fcntl(file, F_SETLKW, &fl);

    write(file, &emp, sizeof(emp));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    printf("[ADMIN] Registered new employee ID %d (%s %s).\n", emp.empID, emp.firstName, emp.lastName);

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Employee #%d successfully registered!^", emp.empID);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
    return 1;
}

/**
 * modifyCE - Updates name fields of customers or employees with write locking.
 */
void modifyCE(int connection_fd, int modifyChoice) {
    char write_buf[4096];
    char readBuffer[4096];

    if (modifyChoice == 1) {
        // Modify Customer
        int accNo;
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Enter Customer Account Number: ");
        write(connection_fd, write_buf, sizeof(write_buf));

        bzero(readBuffer, sizeof(readBuffer));
        if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
        accNo = atoi(readBuffer);

        int file = open(CUSPATH, O_RDWR);
        if (file == -1) return;

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

        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = offset;
        fl.l_len = sizeof(struct Customer);
        fl.l_pid = getpid();
        fcntl(file, F_SETLKW, &fl);

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
    } else if (modifyChoice == 2) {
        // Modify Employee
        int empID;
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Enter Employee ID: ");
        write(connection_fd, write_buf, sizeof(write_buf));

        bzero(readBuffer, sizeof(readBuffer));
        if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
        empID = atoi(readBuffer);

        int file = open(EMPPATH, O_RDWR);
        if (file == -1) return;

        struct Employee emp;
        off_t offset = -1;
        while (read(file, &emp, sizeof(emp)) > 0) {
            if (emp.empID == empID && emp.role != -1) {
                offset = lseek(file, -sizeof(struct Employee), SEEK_CUR);
                break;
            }
        }

        if (offset == -1) {
            close(file);
            bzero(write_buf, sizeof(write_buf));
            strcpy(write_buf, "Employee ID not found!^");
            write(connection_fd, write_buf, sizeof(write_buf));
            read(connection_fd, readBuffer, sizeof(readBuffer));
            return;
        }

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

        bzero(write_buf, sizeof(write_buf));
        snprintf(write_buf, sizeof(write_buf), "Current: %s %s\nEnter New First Name: ", emp.firstName, emp.lastName);
        write(connection_fd, write_buf, sizeof(write_buf));

        bzero(readBuffer, sizeof(readBuffer));
        if (read(connection_fd, readBuffer, sizeof(readBuffer)) > 0) {
            strncpy(emp.firstName, readBuffer, sizeof(emp.firstName) - 1);
            lseek(file, offset, SEEK_SET);
            write(file, &emp, sizeof(emp));
        }

        fl.l_type = F_UNLCK;
        fcntl(file, F_SETLK, &fl);
        close(file);

        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Employee details updated successfully!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
    }
}

/**
 * manageRole - Promotes an employee to manager or demotes a manager to employee.
 * Implements promote_to_manager and demote_to_employee from UML.
 */
void manageRole(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    struct Employee emp;
    int id, choice;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Employee ID: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    id = atoi(readBuffer);

    int file = open(EMPPATH, O_RDWR);
    if (file == -1) return;

    off_t offset = -1;
    while (read(file, &emp, sizeof(emp)) > 0) {
        if (emp.empID == id && emp.role != -1) {
            offset = lseek(file, -sizeof(struct Employee), SEEK_CUR);
            break;
        }
    }

    if (offset == -1) {
        close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Employee ID not found!^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

    // Lock employee record
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

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf),
             "Staff ID: %d (%s %s) | Current Role: %s\n1. Promote to Manager\n2. Demote to Employee\nEnter choice: ",
             emp.empID, emp.firstName, emp.lastName,
             (emp.role == 0 ? "Manager" : "Employee"));
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) > 0) {
        choice = atoi(readBuffer);
        if (choice == 1) {
            emp.role = 0; // Manager
            printf("[ADMIN] Employee %d promoted to Manager.\n", id);
        } else if (choice == 2) {
            emp.role = 1; // Employee
            printf("[ADMIN] Manager %d demoted to Employee.\n", id);
        }

        lseek(file, offset, SEEK_SET);
        write(file, &emp, sizeof(emp));
    }

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "User role for ID #%d updated successfully!^", id);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * viewAdminCustomerDetails - Displays customer record attributes.
 */
void viewAdminCustomerDetails(int connection_fd) {
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
                         "\n--- Customer Account Details ---\nAccount Number : %d\nFull Name      : %s\nBalance        : %.2f\nStatus         : %s\n^",
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
 * viewAdminEmployeeDetails - Displays staff record attributes.
 */
void viewAdminEmployeeDetails(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    struct Employee emp;
    int empID, found = 0;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Staff / Employee ID: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    empID = atoi(readBuffer);

    int file = open(EMPPATH, O_RDONLY);
    if (file != -1) {
        while (read(file, &emp, sizeof(emp)) > 0) {
            if (emp.empID == empID && emp.role != -1) {
                found = 1;
                bzero(write_buf, sizeof(write_buf));
                snprintf(write_buf, sizeof(write_buf),
                         "\n--- Staff Details ---\nStaff ID  : %d\nFull Name : %s %s\nRole      : %s\n^",
                         emp.empID, emp.firstName, emp.lastName,
                         (emp.role == 0 ? "Manager" : "Employee"));
                break;
            }
        }
        close(file);
    }

    if (!found) {
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Staff record not found!^");
    }

    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * adminDeleteCustomer - Deletes a customer account (delete_customer from UML).
 */
void adminDeleteCustomer(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    struct Customer cs;
    int accNo;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Customer Account Number to delete: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    accNo = atoi(readBuffer);

    int file = open(CUSPATH, O_RDWR);
    if (file == -1) return;

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
        strcpy(write_buf, "Customer account not found or already deleted^");
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

    cs.activeStatus = -1; // Soft delete

    lseek(file, offset, SEEK_SET);
    write(file, &cs, sizeof(cs));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    printf("[ADMIN] Customer account #%d deleted.\n", accNo);

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Customer account #%d successfully deleted!^", accNo);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * adminDeleteEmployee - Deletes an employee record (delete_employee from UML).
 */
void adminDeleteEmployee(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    struct Employee emp;
    int id;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter Employee ID to delete: ");
    write(connection_fd, write_buf, sizeof(write_buf));

    bzero(readBuffer, sizeof(readBuffer));
    if (read(connection_fd, readBuffer, sizeof(readBuffer)) <= 0) return;
    id = atoi(readBuffer);

    int file = open(EMPPATH, O_RDWR);
    if (file == -1) return;

    off_t offset = -1;
    while (read(file, &emp, sizeof(emp)) > 0) {
        if (emp.empID == id && emp.role != -1) {
            offset = lseek(file, -sizeof(struct Employee), SEEK_CUR);
            break;
        }
    }

    if (offset == -1) {
        close(file);
        bzero(write_buf, sizeof(write_buf));
        strcpy(write_buf, "Employee ID not found or already deleted^");
        write(connection_fd, write_buf, sizeof(write_buf));
        read(connection_fd, readBuffer, sizeof(readBuffer));
        return;
    }

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

    emp.role = -1; // Soft delete

    lseek(file, offset, SEEK_SET);
    write(file, &emp, sizeof(emp));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    printf("[ADMIN] Employee ID #%d deleted.\n", id);

    bzero(write_buf, sizeof(write_buf));
    snprintf(write_buf, sizeof(write_buf), "Employee #%d successfully deleted!^", id);
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

/**
 * adminChangePassword - Allows the administrator to update the admin master password.
 * Fulfills change_admin_password from the UML diagram.
 */
int adminChangePassword(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];
    char newPassword[256];
    struct Admin admin;

    int file = open(ADMINPATH, O_RDWR | O_CREAT, 0644);
    if (file == -1) return 0;

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "Enter new admin password: ");
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
    fl.l_start = 0;
    fl.l_len = sizeof(struct Admin);
    fl.l_pid = getpid();
    fcntl(file, F_SETLKW, &fl);

    lseek(file, 0, SEEK_SET);
    int bytes = read(file, &admin, sizeof(admin));
    if (bytes <= 0) {
        memset(&admin, 0, sizeof(admin));
        admin.adminID = 1;
        strncpy(admin.firstName, "System", sizeof(admin.firstName) - 1);
        strncpy(admin.lastName, "Admin", sizeof(admin.lastName) - 1);
    }

    strncpy(admin.password, crypt(newPassword, HASHKEY), sizeof(admin.password) - 1);

    lseek(file, 0, SEEK_SET);
    write(file, &admin, sizeof(admin));

    fl.l_type = F_UNLCK;
    fcntl(file, F_SETLK, &fl);
    close(file);

    printf("[SECURITY] Administrator updated master password.\n");
    return 1;
}

/**
 * adminLogout - Releases and unlinks the admin session semaphore.
 */
void adminLogout(int connection_fd) {
    char write_buf[4096];
    char readBuffer[4096];

    sem_t *target_sem = sem_open(ADMIN_SEM_NAME, 0);
    if (target_sem != SEM_FAILED) {
        sem_post(target_sem);
        sem_close(target_sem);
        sem_unlink(ADMIN_SEM_NAME);
    }

    bzero(write_buf, sizeof(write_buf));
    strcpy(write_buf, "^");
    write(connection_fd, write_buf, sizeof(write_buf));
    read(connection_fd, readBuffer, sizeof(readBuffer));
}

#endif // ADMIN_FUNCTIONS_H
