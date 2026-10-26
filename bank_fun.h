#ifndef BANK_FUN_H
#define BANK_FUN_H

/*
 * =====================================================================================
 *  CS-513 System Software - Banking Management System
 *  Data Structure Definitions (Database Schema)
 * =====================================================================================
 *  This header defines the binary record structures stored on disk for all bank entities:
 *  - Customers (accounts, balances, status)
 *  - Employees & Managers (roles, credentials)
 *  - Administrators (system governance)
 *  - Loan Details (applications, state machine)
 *  - Transaction History (audit trail for passbook)
 *  - Feedback (customer feedback & manager review status)
 *  - Counters (atomic sequence generator for loans & feedback)
 * =====================================================================================
 */

/**
 * struct Customer - Represents a bank customer account record in customers.txt
 * 
 * accountNumber : Unique identifier for the account
 * balance       : Current liquid funds available in the account
 * customerName  : Full name of the account holder
 * password      : SHA-512 hashed password string generated via crypt()
 * activeStatus  : Account lifecycle state:
 *                 -1 -> Deleted (soft-deleted by admin/employee)
 *                  0 -> Deactivated (suspended by manager)
 *                  1 -> Activated (active and allowed to transact)
 */
struct Customer {
    int accountNumber;
    float balance;
    char customerName[20];
    char password[256];
    int activeStatus; // -1 -> deleted, 0 -> deactivated, 1 -> activated
};

/**
 * struct Employee - Represents a bank staff member in employees.txt
 * 
 * empID     : Unique employee identification number
 * firstName : Staff member's first name
 * lastName  : Staff member's last name
 * password  : SHA-512 hashed password string
 * role      : Staff role level:
 *             -1 -> Deleted (account revoked)
 *              0 -> Manager (administrative & approval authority)
 *              1 -> Employee (teller / loan review staff)
 */
struct Employee {
    int empID;
    char firstName[20];
    char lastName[20];
    char password[256];
    int role; // -1 -> Deleted, 0 -> Manager, 1 -> Employee
};

/**
 * struct Admin - Represents system administrator credentials in admin.txt
 * Matches UML Class Diagram specifications for the Admin entity.
 */
struct Admin {
    int adminID;
    char firstName[20];
    char lastName[20];
    char password[256];
};

/**
 * struct LoanDetails - Tracks a customer's loan application in loanDetails.txt
 * 
 * empID         : Assigned employee ID (-1 if pending manager assignment)
 * accountNumber : Account number of the applicant
 * loanID        : Unique sequence identifier for the loan
 * loanAmount    : Principal amount requested
 * status        : Current workflow state:
 *                 0 -> Requested (submitted by customer)
 *                 1 -> Pending (assigned to an employee for review)
 *                 2 -> Approved (credited to customer account)
 *                 3 -> Rejected (denied by employee or inactive account)
 */
struct LoanDetails {
    int empID;
    int accountNumber;
    int loanID;
    int loanAmount;
    int status; // 0 -> requested, 1 -> pending, 2 -> approved, 3 -> rejected
};

/**
 * struct trans_histroy - Individual audit log entry in trans_hist.txt
 * Used to implement customer transaction history and employee passbook viewing.
 */
struct trans_histroy {
    int acc_no;
    char hist[1024];
};

/**
 * struct FeedBack - Customer feedback entry in feedback.txt
 * Matches the UML Feedback entity with feedbackID, customerID, message, and review status.
 * Total size is strictly 1024 bytes to preserve binary compatibility with existing records.
 */
struct FeedBack {
    int feedbackID;
    int customerID;
    char feedback[1000];
    int status; // 0 -> Pending / Unreviewed, 1 -> Reviewed by Manager
    char padding[12]; // Maintains exact 1024-byte record boundary
};

/**
 * struct Counter - Monotonically increasing sequence generator
 * Used by loanCounter.txt to assign guaranteed unique IDs.
 */
struct Counter {
    int count;
};

#endif // BANK_FUN_H
