#include "tftp.h"

static void handle_client(struct sockaddr_in client_addr, socklen_t client_len, tftp_packet *packet,int transfer_port);
static int make_data_socket(int port);

/*<----------- server main -------->*/
int main() 
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    tftp_packet packet;

    //Reap finished per-transfer children automatically, no zombies
    signal(SIGCHLD, SIG_IGN);

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd == -1)
    {
        perror("socket");
        exit(1);
    }
    
    // Set up server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket
    if(bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind");
        close(sockfd);
        exit(1);
    }
    printf("TFTP Server listening on port %d...\n", PORT);
    fflush(stdout);
    int next_transfer_port = 20000;
    // Main loop to handle incoming requests
    while (1) 
    {
        memset(&packet, 0, sizeof(packet));
        client_len = sizeof(client_addr);

        int n = recvfrom(sockfd, &packet, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
        if (n < 0) 
        {
            if (errno != EINTR)
                perror("Receive failed or timeout occurred");
            continue;
        }
        if (n < 4)// Too short to be a TFTP packet
        {
            continue;
        }

        /*
        * Creating the process for executing the handle_client function
        * if any error comes means the child terminating by using exit system call main will not effected.
        */
        int transfer_port = next_transfer_port++;
        if (next_transfer_port > 60000)
            next_transfer_port = 20000;

        pid_t pid = fork();
        if (pid == 0)
        {
            close(sockfd);
            handle_client(client_addr, client_len, &packet, transfer_port);
            exit(0);
        }
        else if (pid < 0)
        {
            perror("fork");
        }
    }
}

// makeing default socket (if the port is busy the next ones are tried)
static int make_data_socket(int port)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == -1)
    {
        perror("socket(per-transfer)");
        return -1;
    }
    struct timeval tv = { .tv_sec = TIMEOUT_SEC, .tv_usec = 0 };
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,&tv, sizeof(tv)) == -1)
    {
        perror("setsockopt");
        close(s);
        return -1;
    }
    struct sockaddr_in any_addr;

    for (int tries = 0; tries < 100; tries++)
    {
        memset(&any_addr, 0, sizeof(any_addr));

        any_addr.sin_family = AF_INET;
        any_addr.sin_addr.s_addr = INADDR_ANY;
        any_addr.sin_port = htons(port);

        if (bind(s, (struct sockaddr *)&any_addr,sizeof(any_addr)) == 0)
        {
            printf("Transfer socket bound to port %d\n", port);
            fflush(stdout);
            return s;
        }
        if (errno != EADDRINUSE)
            break;
        port++;
        if (port > 60000)
            port = 20000;
    }
    perror("bind(per-transfer)");
    close(s);
    return -1;
}

// error sending function 
static void send_error(int sockfd, struct sockaddr_in *addr, socklen_t addr_len, uint16_t code, const char *msg)
{
    tftp_packet err;
    memset(&err, 0, sizeof(err));
    err.opcode = htons(ERROR);
    err.body.error_packet.error_code = htons(code); // Set TFTP error code
    // Copy error message into packet
    strncpy(err.body.error_packet.error_msg, msg, sizeof(err.body.error_packet.error_msg)-1);

    // Calculate actual size of the error packet
    size_t len = sizeof(err.opcode) + sizeof(err.body.error_packet.error_code) + strlen(err.body.error_packet.error_msg) + 1;
    sendto(sockfd, &err, len, 0, (struct sockaddr *)addr, addr_len);// Send error packet to the client
}

// Tell the client why an open() failed
static void report_open_error(int sockfd, struct sockaddr_in *addr, socklen_t addr_len, int err)
{
    switch (err)
    {
        case ENOENT:
            send_error(sockfd, addr, addr_len, ERR_FILE_NOT_FOUND, "File not found");
            break;
        case EACCES:
        case EPERM:
            send_error(sockfd, addr, addr_len, ERR_ACCESS, "Access violation");
            break;
        case EEXIST:
            send_error(sockfd, addr, addr_len, ERR_FILE_EXISTS, "File already exist");
            break;
        default:
            send_error(sockfd, addr, addr_len, ERR_NOT_DEFINED, strerror(err));
    }
}

// client handler function 
static void handle_client(struct sockaddr_in client_addr, socklen_t client_len, tftp_packet *packet, int transfer_port) 
{
    // Extract the TFTP operation (read or write) from the received packet
    // and call send_file or receive_file accordingly

    // converting network data to host data type...
    uint16_t opcode = ntohs(packet->opcode);

    if(opcode != RRQ && opcode != WRQ)
    {
        fprintf(stderr, "Unexpected opcode %u from client, ignoring\n", opcode);
        return;
    }

    /*
     * The request is "opcode | filename 0 | mode 0". The packet was zeroed before
     * recvfrom, and the last byte is forced to 0 so both strings are terminated.
     */
    char *raw = (char *)packet;
    raw[sizeof(*packet) - 1] = '\0';
    char *filename = raw + 2;
    size_t fname_len = strlen(filename);
    if (2 + fname_len + 1 >= sizeof(*packet))// no room for a mode string
    {
        fprintf(stderr, "Malformed request\n");
        return;
    }
    char *mode_raw = filename + fname_len + 1;

    // Create a separate UDP socket for this transfer
    int data_sock = make_data_socket(transfer_port);
    if(data_sock == -1)
    {
        return;
    }

    // Validate the requested filename
    if (sanitize_filename(filename) == -1)
    {
        fprintf(stderr, "Rejected unsafe filename: %s\n", filename);
        // Send invalid filename error to client
        send_error(data_sock, &client_addr, client_len, ERR_ACCESS, "Invalid filename");
        close(data_sock);
        return;
    }

    // Only octet and netascii are supported
    char *mode = (char *)canonical_mode(mode_raw);
    if (mode == NULL)
    {
        fprintf(stderr, "Rejected unsupported mode: %.20s\n", mode_raw);
        send_error(data_sock, &client_addr, client_len, ERR_ILLEGAL_OP, "Unsupported transfer mode");
        close(data_sock);
        return;
    }

    printf("%s request: file=%s mode=%s\n", opcode == RRQ ? "RRQ" : "WRQ", filename, mode);
    fflush(stdout);

    const int block_size = 512;// TFTP DATA packet size is normally 512 bytes

    if(opcode == RRQ)// Handle Read Request
    {
        // Open requested file in read-only mode
        int fd = open(filename, O_RDONLY);
        if(fd == -1)// Check whether file opening failed
        {
            int err = errno;// keep errno safe from later calls
            fprintf(stderr, "open (RRQ): %s\n", strerror(err));
            // Inform client why the file cannot be read
            report_open_error(data_sock, &client_addr, client_len, err);
            close(data_sock);
            return;
        }
        // Send the requested file to the client
        int rc = send_file(data_sock, client_addr, client_len, fd, block_size, mode);
        close(fd);
        printf("RRQ %s: %s\n", filename, rc == 0 ? "complete" : "failed");
    }
    else
    {
         // Create requested file for writing; O_EXCL: never overwrite an existing file
        int fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd == -1)
        {
            int err = errno;
            fprintf(stderr, "open (WRQ): %s\n", strerror(err));
            // Inform client that file cannot be created
            report_open_error(data_sock, &client_addr, client_len, err);
            close(data_sock);
            return;
        }
 
        tftp_packet ack0;
        memset(&ack0, 0, sizeof(ack0));
        ack0.opcode = htons(ACK);
        ack0.body.ack_packet.block_number = htons(0);
        // Send ACK 0 to tell client that server is ready to receive data
        sendto(data_sock, &ack0, sizeof(ack0.opcode) + sizeof(ack0.body.ack_packet.block_number), 0, (struct sockaddr *)&client_addr, client_len);
 
        // Receive the file from the client
        int rc = receive_file(data_sock, client_addr, client_len, fd, block_size, mode);
        close(fd);
        if (rc != 0)
        {
            unlink(filename);// Don't leave a partial file behind
        }
        printf("WRQ %s: %s\n", filename, rc == 0 ? "complete" : "failed");
    }
    fflush(stdout);
 
    close(data_sock);
    
}