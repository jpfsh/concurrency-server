#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// DO NOT MODIFY THE CONTENTS OF THIS FILE. 
// FILE WILL BE REPLACED DURING GRADING.

// These are the message types for the protocol
enum msg_types {
    DONATE,
    CINFO,
    TOP,
    LOGOUT,
    STATS,
    ERROR = 0xFF
};

typedef struct {
    uint64_t totalDonationAmt; // Sum of donations made to charity by donors (clients)
    uint64_t topDonation;      // Largest donation made to this charity
    uint32_t numDonations;     // Count of donations made to charity
} charity_t; 

// This is the struct for each message sent to the server
typedef struct { 
    uint8_t msgtype;  
    union {
        uint64_t maxDonations[3];  // For TOP
        charity_t charityInfo;     // For CINFO response from Server
        struct {uint8_t charity; uint64_t amount;} donation; // For DONATE & CINFO from client
        struct {uint8_t charityID_high; uint8_t charityID_low; 
        uint64_t amount_high; uint64_t amount_low;} stats;   // For STATS (part 2 only)
    } msgdata; 
} message_t;

#endif