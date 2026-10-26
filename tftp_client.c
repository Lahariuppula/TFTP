#include "tftp.h"
#include "errno.h"
#include "tftp_client.h"

int connect_flag = 0;

// Discard any packets remaining from a previous transfer
static void drain_socket(int sockfd)
{
    char b[1024];
    // Receive available packets without blocking until the queue is empty 
    while (recvfrom(sockfd, b, sizeof(b), MSG_DONTWAIT, NULL, NULL) >= 0);
}

static void print_help(void)
{
    printf("connect <server-ip>   : Connect to server\n");
    printf("get <file-name>       : Receive a file from server\n");
    printf("put <file-name>       : Send a file to the server\n");
    printf("mode                  : Transfer mode(s)[octet/netascii]\n");
    printf("disconnect            : Close the connection\n");
    printf("bye / quit            : Exit the application\n");
}

// Select the transfer mode by name
static void set_mode(tftp_client_t *client, const char *name)
{
    if (strcasecmp(name, "mail") == 0)
    {
        printf("ERROR: mail mode is obsolete and not supported\n");
        return;
    }
    const char *m = canonical_mode(name);
    if (m == NULL)
    {
        printf("ERROR: Invalid mode (use octet or netascii)\n");
        return;
    }
    strcpy(client->mode_choice, m);
    printf("INFO: Transfer Mode: %s\n", client->mode_choice);
}

// client main 
int main(int argc, char **argv) 
{
    char command[100];
    tftp_client_t client;

    memset(&client, 0, sizeof(client));  // Initialize client structure

    strcpy(client.mode_choice, "netascii");
    client.data_size = 512;
    tftp_show_progress = 1;

    if (argc > 2)
    {
        fprintf(stderr, "Usage: %s [server-ip]\n", argv[0]);
        return 1;
    }
    if (argc == 2)// Optional: connect straight from the command line
    {
        connect_to_server(&client, argv[1], PORT);
    }

    printf("Type help to get supported features\n");
    // Main loop for command-line interface
    while (1) 
    {
        printf("tftp] ");
        fflush(stdout);
        if(fgets(command, sizeof(command), stdin) == NULL)// Ctrl-D / EOF
        {
            printf("\n");
            disconnect(&client);
            break;
        }

        if(strchr(command, '\n') == NULL)// Line longer than the buffer
        {
            int ch;
            while((ch = getchar()) != '\n' && ch != EOF)
                ;
            printf("Invalid command\n");
            continue;
        }

        // Remove newline character
        command[strcspn(command, "\n")] = 0;

        if(strcmp(command, "bye") == 0 || strcmp(command, "quit") == 0)
        {
            printf("App is closing....\n");
            disconnect(&client);
            break;
        }
        // Process the command
        process_command(&client, command);
    }

    return 0;
}

// Function to process commands
void process_command(tftp_client_t *client, char *command) 
{
    char *cmd = strtok(command, " \t\r");
    char *arg = strtok(NULL, " \t\r");
    char *extra = strtok(NULL, " \t\r");

    if(cmd == NULL)// Empty line
    {
        return;
    }

    if(strcmp(cmd, "help") == 0)
    {
        print_help();
    }
    else if(strcmp(cmd, "connect") == 0)
    {
        if(arg == NULL || extra != NULL)
        {
            printf("Usage: connect <server-ip>\n");
        }
        else if(connect_flag == 0)// Check whether the client is already connected
        {
            connect_to_server(client, arg, PORT);// Connect client socket to the server
        }
        else
        {
            printf("ERROR: Already connected to %s\n", client->server_ip);
        }
    }
    else if(strcmp(cmd, "put") == 0 || strcmp(cmd, "get") == 0)
    {
        char *filename = arg;

        if(filename == NULL || extra != NULL)
        {
            printf("Usage: %s <file-name>\n", cmd);
        }
        else if(connect_flag == 0)// Check whether client is connected
        {
            printf("ERROR: Not connected to server\n");
        }
        else if(is_file_valid(filename) == -1)// Validate the filename
        {
            printf("ERROR: Invalid file name\n");
        }
        else if(cmd[0] == 'p')
        {
            put_file(client, filename);// Upload file to the server
        }
        else
        {
            get_file(client, filename);// Download file from the server
        }
    }
    else if(strcmp(cmd, "mode") == 0)
    {
        if(arg != NULL)
        {
            set_mode(client, arg);
        }
        else
        {
            mode_file(client);// Change the file transfer mode (menu)
        }
    }
    else if(strcmp(cmd, "disconnect") == 0)
    {
        if(connect_flag == 1)
        {
            disconnect(client);// Disconnect from the server
            printf("INFO: Disconnected\n");
        }
        else
        {
            printf("ERROR: Not connected to server\n");
        }
    }
    else
    {
        printf("Invalid command\n");
    }
}

// File size changing 
void mode_file(tftp_client_t *client)
{
    char line[32];

    // Display available transfer modes
    printf("\n1.Default (Netascii)\n");
    printf("2.Octet\n");
    printf("3.Netascii\n");

    printf("Enter choice : ");

    if(fgets(line, sizeof(line), stdin) == NULL)
    {
        printf("Invalid choice\n");
        return;
    }

    switch(atoi(line))// Process selected mode
    {
        case 1:
            // Default 
            set_mode(client, "netascii");
            client->data_size = 512;// Set default data block size
            break;

        case 2:
            set_mode(client, "octet");// Set transfer mode to octet
            client->data_size = 512;// Set octet data block size
            break;

        case 3:
            set_mode(client, "netascii");// Set transfer mode to netascii
            client->data_size = 512;// Set netascii data block size
            break;

        default:
            printf("Invalid choice\n");
    }
}

// File validation
int is_file_valid(char *filename)
{
    int len = strlen(filename);// Calculate filename length
    if(len == 0 || len > 255)
    {
        return -1;
    }

    const char *illegal = "/\\:*?\"<>|";// List of characters that are not allowed in filename

    for(int i = 0; i < len; i++)// Check every character in filename
    {
        if(strchr(illegal, filename[i]) != NULL)// Check whether current character is illegal
        {
            return -1;
        }
    }

    // Names such as "README" or "syslog" (no extension) are valid
    if(strcmp(filename, ".") == 0 || strstr(filename, "..") != NULL)
    {
        return -1;
    }

    return 0;
}

// Address validation 
int add_vali(char *ip)
{
    // Check whether IP address is too long
    if (strlen(ip) >= INET_ADDRSTRLEN)
    {
        return 1;
    }

    // Check for consecutive dots or dot at beginning/end
    if (strstr(ip, "..") != NULL || ip[0] == '.' || ip[strlen(ip) - 1] == '.')
    {
        return 1;
    }

    char temp_ip[INET_ADDRSTRLEN];
    strncpy(temp_ip, ip, sizeof(temp_ip) - 1);// Copy IP address into temporary buffer
    temp_ip[sizeof(temp_ip) - 1] = '\0';// Ensure temporary string is NULL terminated

    int segment_count = 0;
    char *token = strtok(temp_ip, "."); // Split IP address using dot separator

    while (token != NULL)// Process every IP segment
    {
        segment_count++;
        int i = 0;
        while (token[i] != '\0')// Check every character in current segment
        {
            if (!isdigit((unsigned char) token[i]))// Check whether character is a digit
            {
                return 1;
            }
            i++;
        }

        int value = atoi(token);// Convert current segment from string to integer
        if (value < 0 || value > 255)
        {
            return 1;
        }

        token = strtok(NULL, ".");// Get next IP segment
    }

    if (segment_count != 4)
    {
        return 1;
    }

    return 0;
}

// connecting the server ip address 
void connect_to_server(tftp_client_t *client, char *ip, int port) 
{
    //ip_address validating
    int ret = add_vali(ip);
    if(ret == 1)
    {
        connect_flag = 0;
        printf("ERROR: Invalid IP address\n");
        return;
    }

    //copying the valid ip_address...
    strcpy(client->server_ip, ip);
    
    // Create UDP socket
    client-> sockfd = socket(AF_INET, SOCK_DGRAM,0);

    if (client->sockfd == -1)
    {
        connect_flag = 0;
        printf("ERROR: socket() failed: %s\n", strerror(errno));
        return;
    }

    // Set socket timeout option
    struct timeval tv = { .tv_sec = TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(client->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Set up server address
    memset(&client-> server_addr, 0, sizeof(client-> server_addr));
    client-> server_addr.sin_family = AF_INET;
    client-> server_addr.sin_port = htons(port);
    client-> server_addr.sin_addr.s_addr = inet_addr(client->server_ip);

    client-> server_len = sizeof(client-> server_addr);// Store size of server address structure
    
    connect_flag = 1;
    printf("INFO: Connected to server\n");
}

// put function 
void put_file(tftp_client_t *client, char *filename)
{
    // Reset destination to well-known TFTP server port
    client->server_addr.sin_port = htons(PORT);

    int fd = open(filename, O_RDONLY);

    if(fd == -1)
    {  
        if(errno == ENOENT)
            printf("ERROR: File not found\n");
        else
            printf("ERROR: open: %s\n", strerror(errno));
        return;
    }

    printf("INFO: File: %s\n", filename);
    printf("INFO: Transfer Mode: %s\n", client->mode_choice);

    tftp_packet D_packet;
    struct sockaddr_in request_addr = client->server_addr;
    request_addr.sin_port = htons(PORT);
    struct sockaddr_in transfer_addr; 
    socklen_t transfer_len = sizeof(transfer_addr); 
    int got_ack = 0;

    drain_socket(client->sockfd);

    // Send WRQ; repeat it if the server does not answer
    for(int attempt = 0; attempt < REQUEST_TRIES && !got_ack; attempt++)
    {
        send_request(client->sockfd, request_addr, filename, client->mode_choice, WRQ);

        // Wait for ACK block 0 from server
        while(1)
        {
            memset(&D_packet, 0, sizeof(D_packet));
            transfer_len = sizeof(transfer_addr); 
            ssize_t recv_size = recvfrom( client->sockfd, &D_packet, sizeof(D_packet), 0, (struct sockaddr *)&transfer_addr, &transfer_len );
            if(recv_size == -1)
            {
                if(errno == EINTR)
                {
                    continue;
                }
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;// timeout: send the WRQ again
                }
                perror("recvfrom");
                close(fd);
                return;
            }

            if(transfer_addr.sin_addr.s_addr != request_addr.sin_addr.s_addr || recv_size < 4)
            {
                continue;
            }

            //checking the returned ack bit...
            if(ntohs(D_packet.opcode) == ACK && ntohs(D_packet.body.ack_packet.block_number) == 0)
            {
                got_ack = 1;
                break;
            }
            else if (ntohs(D_packet.opcode) == ERROR)
            {
                printf("ERROR: %s\n", packet_error_msg(&D_packet, recv_size));
                close(fd);
                return;
            }
        }
    }

    if(!got_ack)
    {
        printf("ERROR: Server not responding\n");
        close(fd);
        return;
    }

    // Send file data to server
    int rc = send_file( client->sockfd, transfer_addr, transfer_len, fd, client->data_size, client->mode_choice );
    close(fd);
    if(rc == 0)
    {
        printf("INFO: File transfer successful\n");
    }
}

// get function 
void get_file(tftp_client_t *client, char *filename) 
{
    /* Open the destination file; O_EXCL: never overwrite an existing file */
    int fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd == -1)
    {
        if(errno == EEXIST)
            printf("ERROR: File already exist\n");
        else
            printf("ERROR: open: %s\n", strerror(errno));
        return;
    }

    printf("INFO: File: %s\n", filename);
    printf("INFO: Transfer Mode: %s\n", client->mode_choice);

    /* Send RRQ to the server's well-known port */
    struct sockaddr_in request_addr = client->server_addr;
    request_addr.sin_port = htons(PORT);
    int got_reply = 0;

    drain_socket(client->sockfd);

    // Send RRQ; repeat it if the server does not answer
    for(int attempt = 0; attempt < REQUEST_TRIES && !got_reply; attempt++)
    {
        send_request(client->sockfd, request_addr, filename, client->mode_choice, RRQ);

        struct pollfd pfd = { .fd = client->sockfd, .events = POLLIN };
        if(poll(&pfd, 1, TIMEOUT_SEC * 1000) > 0)
        {
            got_reply = 1;
        }
    }

    if(!got_reply)
    {
        printf("ERROR: Server not responding\n");
        close(fd);
        unlink(filename);
        return;
    }

    /*
     * Receive all DATA blocks, write them to the file,
     * and acknowledge each block.
     */
    int rc = receive_file(client->sockfd,request_addr,sizeof(request_addr),fd,client->data_size,client->mode_choice);
    close(fd);
    if(rc == 0)
    {
        printf("INFO: File receive successful\n");
    }
    else
    {
        unlink(filename);// Remove the empty/partial file
    }
}

// Disconnecting 
void disconnect(tftp_client_t *client) 
{
    // close fd
    if(client->sockfd > 0)// Check whether socket descriptor is valid
    {
        close(client->sockfd);
        client->sockfd = 0;
    }
    connect_flag = 0;
}

// send request 
void send_request(int sockfd, struct sockaddr_in server_addr, char *filename, char *mode,int opcode)
{
    tftp_packet client;
    memset(&client, 0, sizeof(client));
    client.opcode = htons(opcode);

    // RFC 1350 request: opcode | filename 0 | mode 0, packed back to back
    char *p = (char *)&client.body;
    strncpy(p, filename, 255);
    p += strlen(p) + 1;// filename + terminating 0
    strncpy(p, mode, 19);
    p += strlen(p) + 1;// mode + terminating 0

    // Calculate actual request packet length
    size_t packet_len = (size_t)((char *)p - (char *)&client);

    // Send RRQ or WRQ packet to server
    ssize_t s_size = sendto(sockfd, &client, packet_len, 0,(struct sockaddr *)&server_addr, sizeof(server_addr));
    if(s_size == -1)
    {
        perror("sendto");
        return;
    }
}