/* Common file for server & client*/

#ifndef TFTP_H
#define TFTP_H

#include <stdio.h>
#include <string.h> 
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <sys/time.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <strings.h>

#define PORT 6969
#define BUFFER_SIZE 516  // TFTP data packet size (512 bytes data + 4 bytes header)
#define TIMEOUT_SEC 5    // Timeout in seconds
#define MAX_RETRIES 10   // Timeouts tolerated while sending/receiving
#define REQUEST_TRIES 3  // Times an RRQ/WRQ is sent before giving up

/* TFTP OpCodes */
typedef enum 
{
    RRQ = 1,  // Read Request
    WRQ = 2,  // Write Request
    DATA = 3, // Data Packet
    ACK = 4,  // Acknowledgment
    ERROR = 5 // Error Packet
} tftp_opcode;

/* TFTP Error codes */
#define ERR_NOT_DEFINED    0
#define ERR_FILE_NOT_FOUND 1
#define ERR_ACCESS         2
#define ERR_DISK_FULL      3
#define ERR_ILLEGAL_OP     4
#define ERR_UNKNOWN_TID    5
#define ERR_FILE_EXISTS    6

/* TFTP Packet Structure */
typedef struct {
    uint16_t opcode; // Operation code (RRQ/WRQ/DATA/ACK/ERROR)
    union {
        struct {
            char filename[256];
            char mode[20];  // "netascii"or"octet"
        } request;  // RRQ and WRQ
        struct {
            uint16_t block_number;
            char data[512];
        } data_packet; // DATA
        struct {
            uint16_t block_number;
        } ack_packet; // ACK
        struct {
            uint16_t error_code;
            char error_msg[512];
        } error_packet; // ERROR
    } body;
} tftp_packet;

/* Set to 1 by the client to print '#' progress marks */
extern int tftp_show_progress;

/*Function Prototypes*/
int send_file(int sockfd, struct sockaddr_in client_addr, socklen_t client_len, int fd, int size, char *mode);
int receive_file(int sockfd, struct sockaddr_in client_addr, socklen_t client_len, int fd, int size, char *mode);

void convert_to_netascii(char *data, ssize_t *size);
void convert_from_netascii(char *data, ssize_t *size);

int sanitize_filename(const char *filename);

const char *canonical_mode(const char *mode);           
const char *packet_error_msg(tftp_packet *pkt, ssize_t n); 

#endif // TFTP_H