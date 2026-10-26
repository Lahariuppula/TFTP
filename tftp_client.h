#ifndef TFTP_CLIENT_H
#define TFTP_CLIENT_H

typedef struct 
{
    int sockfd;
    char mode_choice[20];
    int data_size;
    struct sockaddr_in server_addr;
    socklen_t server_len;
    char server_ip[INET_ADDRSTRLEN];
} tftp_client_t;

// Function prototypes
void connect_to_server(tftp_client_t *client, char *ip, int port);
void put_file(tftp_client_t *client, char *filename);
void get_file(tftp_client_t *client, char *filename);
void disconnect(tftp_client_t *client);
void process_command(tftp_client_t *client, char *command);
int add_vali(char *ip);
int is_file_valid(char *filename);
void mode_file(tftp_client_t *client);

void send_request(int sockfd, struct sockaddr_in server_addr, char *filename, char *mode,int opcode);

#endif