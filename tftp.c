/* Common file for server & client */

#include "tftp.h"
#include "tftp_client.h"

int tftp_show_progress = 0;
static int hash_on_line = 0;
static int na_last_cr = 0;   // CR left over at the end of the previous netascii block

/* ---------- small helpers ---------- */

static void tftp_err(const char *fmt, ...)
{
    va_list ap;
    if (hash_on_line)
    {
        putchar('\n');
        hash_on_line = 0;
    }
    fflush(stdout);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void progress_tick(uint16_t blk)
{
    if (tftp_show_progress && (blk == 1 || blk % 50 == 0))
    {
        putchar('#');
        fflush(stdout);
        hash_on_line = 1;
    }
}

static void progress_done(void)
{
    if (hash_on_line)
    {
        putchar('\n');
        hash_on_line = 0;
    }
}

static int is_netascii(const char *mode)
{
    return strcasecmp(mode, "netascii") == 0;
}

const char *canonical_mode(const char *mode)
{
    if (strcasecmp(mode, "octet") == 0)    
        return "octet";
    if (strcasecmp(mode, "netascii") == 0) 
        return "netascii";
    return NULL;
}

static int write_all(int fd, const char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t w = write(fd, buf, n);
        if (w < 0)
        {
            if (errno == EINTR) continue;
            return -1;
        }
        buf += w;
        n -= (size_t)w;
    }
    return 0;
}

// Error packet sender used by the shared send/receive code
static void tftp_send_error(int sockfd, struct sockaddr_in *addr, socklen_t addr_len, uint16_t code, const char *msg)
{
    unsigned char buf[4 + 128];
    size_t ml = strlen(msg);
    uint16_t op = htons(ERROR), c = htons(code);

    if (ml > 127) ml = 127;
    memcpy(buf, &op, 2);
    memcpy(buf + 2, &c, 2);
    memcpy(buf + 4, msg, ml);
    buf[4 + ml] = 0;
    sendto(sockfd, buf, 5 + ml, 0, (struct sockaddr *)addr, addr_len);
}

const char *packet_error_msg(tftp_packet *pkt, ssize_t n)
{
    if (n < 5) 
        return "";
    if ((size_t)n >= sizeof(*pkt)) 
        n = (ssize_t)sizeof(*pkt) - 1;
    ((char *)pkt)[n] = '\0';
    return pkt->body.error_packet.error_msg;
}

static void send_ack(int sockfd, struct sockaddr_in *addr, socklen_t len, uint16_t blk, unsigned char *copy)
{
    unsigned char ack[4];
    uint16_t op = htons(ACK), b = htons(blk);

    memcpy(ack, &op, 2);
    memcpy(ack + 2, &b, 2);
    if (copy) memcpy(copy, ack, 4);
    sendto(sockfd, ack, 4, 0, (struct sockaddr *)addr, len);
}

//converting to windows file
/*
 * '\n' -> CR LF, '\r' -> CR NUL (RFC 1350 netascii).
 * data holds *size (<= 512) raw bytes and must have room for 2 * (*size)
 * bytes; the expanded result is written back and *size is updated.
 */
void convert_to_netascii(char *data, ssize_t *size)
{
    char temp[1024];
    int j = 0; 

    for (int i = 0; i < *size; i++)// Process every byte of input data
    {
        if (j + 2 > (int)sizeof(temp))// Check temporary buffer space
        {
            break;
        }
        if (data[i] == '\n')
        {
            temp[j++] = '\r';// Add carriage return before newline
            temp[j++] = '\n';// Add newline character
        }
        else if (data[i] == '\r')
        {
            temp[j++] = '\r';// A bare CR is sent as CR NUL
            temp[j++] = '\0';
        }
        else
        {
            temp[j++] = data[i];// Copy normal character
        }
    }

    memcpy(data, temp, j);//Copy converted data back to original buffer

    *size = j;// Update size after conversion
}

//converting to linux file 
/*
 * CR LF -> '\n', CR NUL -> '\r'. A CR that is the last byte of a block is
 * remembered (na_last_cr) and resolved by the first byte of the next block.
 * data needs room for *size + 1 bytes.
 */
void convert_from_netascii(char *data, ssize_t *size)
{
    char temp[1100];
    int j = 0;

    for (int i = 0; i < *size; i++)// Process every received byte
    {
        unsigned char c = (unsigned char)data[i];

        if (na_last_cr)
        {
            na_last_cr = 0;
            if (c == '\n') { temp[j++] = '\n'; continue; }// Convert CR-LF into Linux newline
            if (c == 0)    { temp[j++] = '\r'; continue; }// CR-NUL is a bare CR
            temp[j++] = '\r';// Lone CR, then handle c normally
        }
        if (c == '\r')
        {
            na_last_cr = 1;
        }
        else
        {
            temp[j++] = (char)c;// Copy normal character
        }
    }

    memcpy(data, temp, j);// Copy converted data back to original buffer

    *size = j;// Update size after conversion
}

/*
 * Basic filename safety check for anything coming coming off the wire from a
 * remote RRQ/WRQ. Rejects absolute paths and parent-directory traversal.
 */
int sanitize_filename(const char *filename)
{
    if (filename == NULL || filename[0] == '\0')//Check for NULL or empty filename
    {
        return -1;
    }

    if (strlen(filename) > 255)// Longer than the request packet allows
    {
        return -1;
    }

    if (filename[0] == '/')// Check for absolute path
    {
        return -1; // no absolute paths
    }

    if (strstr(filename, "..") != NULL)// Check for parent-directory traversal
    {
        return -1; // no parent directory traversal
    }

    return 0;
}

// real send function 

int send_file(int sockfd, struct sockaddr_in client_addr,socklen_t client_len, int fd, int size, char *mode)
{
    tftp_packet packet;
    uint16_t block_number = 1;
    ssize_t bytes_read;
    int netascii = is_netascii(mode);
    char pend[2048];        // netascii bytes that did not fit in the previous block
    ssize_t pend_len = 0;
    int eof = 0;

    if (size <= 0 || size > 512)
        size = 512;

    while (1)
    {
        memset(&packet, 0, sizeof(packet));
        /* Read one block from the file */
        if (netascii)
        {
            /* Expand raw data until a full block is ready; the surplus stays in pend */
            while (pend_len < size && !eof)
            {
                char raw[1024];
                ssize_t n = read(fd, raw, size);
                if (n < 0)
                {
                    if (errno == EINTR) continue;
                    tftp_err("ERROR: read: %s\n", strerror(errno));
                    tftp_send_error(sockfd, &client_addr, client_len, ERR_NOT_DEFINED, strerror(errno));
                    return -1;
                }
                if (n == 0)
                {
                    eof = 1;
                    break;
                }
                convert_to_netascii(raw, &n);
                memcpy(pend + pend_len, raw, n);
                pend_len += n;
            }
            bytes_read = pend_len < size ? pend_len : size;
            memcpy(packet.body.data_packet.data, pend, bytes_read);
            pend_len -= bytes_read;
            memmove(pend, pend + bytes_read, pend_len);
        }
        else
        {
            bytes_read = 0;
            while (bytes_read < size)
            {
                ssize_t n = read(fd, packet.body.data_packet.data + bytes_read, size - bytes_read);
                if (n < 0)
                {
                    if (errno == EINTR) continue;
                    tftp_err("ERROR: read: %s\n", strerror(errno));
                    tftp_send_error(sockfd, &client_addr, client_len, ERR_NOT_DEFINED, strerror(errno));
                    return -1;
                }
                if (n == 0) break;
                bytes_read += n;
            }
        }

        packet.opcode = htons(DATA);
        packet.body.data_packet.block_number = htons(block_number);

        size_t packet_len = 4 + (size_t)bytes_read;
        int retries = 0;
        int acked = 0;

        /* Retransmit this same DATA block until its ACK arrives */
        while (!acked)
        {
            if (retries >= MAX_RETRIES)
            {
                tftp_err("ERROR: No ACK for block %u after %d attempts\n", block_number, MAX_RETRIES);
                return -1;
            }
            if (sendto(sockfd, &packet, packet_len, 0,(struct sockaddr *)&client_addr,client_len) < 0)
            {
                tftp_err("ERROR: sendto DATA: %s\n", strerror(errno));
                return -1;
            }

            /* Wait for the ACK; stale or foreign packets are ignored WITHOUT resending DATA */
            while (1)
            {
                tftp_packet response;
                struct sockaddr_in response_addr;
                socklen_t response_len = sizeof(response_addr);
                memset(&response, 0, sizeof(response));

                ssize_t ack_size = recvfrom(sockfd, &response, sizeof(response), 0,(struct sockaddr *)&response_addr, &response_len);

                if (ack_size < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        retries++;      // timeout: resend the same block
                        break;
                    }
                    tftp_err("ERROR: recvfrom ACK: %s\n", strerror(errno));
                    return -1;
                }

                if (response_addr.sin_addr.s_addr != client_addr.sin_addr.s_addr || response_addr.sin_port != client_addr.sin_port)
                {
                    tftp_send_error(sockfd, &response_addr, response_len, ERR_UNKNOWN_TID, "Unknown transfer ID");
                    continue;
                }
                if (ack_size < 4)
                {
                    continue;
                }
                if (ntohs(response.opcode) == ERROR)
                {
                    tftp_err("ERROR: %s\n", packet_error_msg(&response, ack_size));
                    return -1;
                }
                if (ntohs(response.opcode) == ACK && ntohs(response.body.ack_packet.block_number) == block_number)
                {
                    acked = 1;
                    break;
                }
                /* Anything else is a stale ACK: ignore it */
            }
        }

        progress_tick(block_number);

        /* A short DATA block marks the end of the transfer */
        if (bytes_read < size)
        {
            progress_done();
            return 0;
        }
        block_number++;
    }
}


// real receive function
int receive_file(int sockfd, struct sockaddr_in client_addr,socklen_t client_len, int fd, int size, char *mode)
{
    tftp_packet rec_data;
    ssize_t recv_size;
    uint16_t block_number = 1;
    int retries = 0;
    const int max_retries = MAX_RETRIES;
    int netascii = is_netascii(mode);
    int locked = (client_addr.sin_port != htons(PORT));   // peer TID already known?
    int dally_sec = locked ? TIMEOUT_SEC : 0;
    unsigned char last_pkt[4];
    size_t last_len = 0;
    char conv[BUFFER_SIZE + 8];

    if (size <= 0 || size > 512)
        size = 512;

    na_last_cr = 0;
    if (locked)     // WRQ on the server: our ACK 0 is the packet to repeat on timeout
    {
        last_pkt[0] = 0; last_pkt[1] = ACK; last_pkt[2] = 0; last_pkt[3] = 0;
        last_len = 4;   // the caller already sent ACK 0 once
    }

    while (1)
    {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        memset(&rec_data, 0, sizeof(rec_data));
        recv_size = recvfrom(sockfd, &rec_data, sizeof(rec_data), 0,(struct sockaddr *)&from, &from_len);
        if (recv_size < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                retries++;
                if (retries >= max_retries)
                {
                    tftp_err("ERROR: Timeout, no response from peer\n");
                    return -1;
                }
                /* Repeat our last ACK so the sender retransmits its DATA block */
                if (last_len)
                {
                    sendto(sockfd, last_pkt, last_len, 0,(struct sockaddr *)&client_addr, client_len);
                }
                continue;
            }
            tftp_err("ERROR: recvfrom: %s\n", strerror(errno));
            return -1;
        }

        /* Only the agreed peer (TID) may talk to us */
        if (from.sin_addr.s_addr != client_addr.sin_addr.s_addr || (locked && from.sin_port != client_addr.sin_port))
        {
            tftp_send_error(sockfd, &from, from_len, ERR_UNKNOWN_TID, "Unknown transfer ID");
            continue;
        }
        if (recv_size < 4)
        {
            continue;
        }
        if (!locked)    // first reply fixes the server's transfer port
        {
            uint16_t first_op = ntohs(rec_data.opcode);
            if (first_op != DATA && first_op != ERROR)
                continue;
            client_addr.sin_port = from.sin_port;
            locked = 1;
        }

        retries = 0;
        if (ntohs(rec_data.opcode) == ERROR)
        {
            tftp_err("ERROR: %s\n", packet_error_msg(&rec_data, recv_size));
            return -1;
        }
        if (ntohs(rec_data.opcode) != DATA)
            continue;

        uint16_t recv_block = ntohs(rec_data.body.data_packet.block_number);

        /* Handle a duplicate DATA block */
        if (recv_block == (uint16_t)(block_number - 1))
        {
            send_ack(sockfd, &client_addr, client_len, recv_block, NULL);
            continue;
        }

        /* Ignore unexpected block numbers */
        if (recv_block != block_number)
            continue;

        ssize_t data_size = recv_size - 4;
        int last = data_size < size;    // decided on the RAW size, before any conversion

        memcpy(conv, rec_data.body.data_packet.data, data_size);
        if (netascii)
        {
            convert_from_netascii(conv, &data_size);
            if (last && na_last_cr)     // file ended with a lone CR
            {
                conv[data_size++] = '\r';
                na_last_cr = 0;
            }
        }

        if (data_size > 0 && write_all(fd, conv, data_size) < 0)
        {
            int e = errno;
            tftp_err("ERROR: write: %s\n", strerror(e));
            if (e == ENOSPC || e == EDQUOT)
                tftp_send_error(sockfd, &client_addr, client_len, ERR_DISK_FULL, "Disk full or allocation exceeded");
            else
                tftp_send_error(sockfd, &client_addr, client_len, ERR_NOT_DEFINED, strerror(e));
            return -1;
        }

        /* Acknowledge the received DATA block */
        send_ack(sockfd, &client_addr, client_len, block_number, last_pkt);
        last_len = 4;
        progress_tick(block_number);

        /* A short DATA block marks the end of the transfer */
        if (last)
        {
            if (dally_sec > 0)
            {
                /* Stay briefly: if the final ACK was lost the sender repeats the last DATA */
                struct timeval tv = { .tv_sec = dally_sec, .tv_usec = 0 };
                setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                while (1)
                {
                    from_len = sizeof(from);
                    recv_size = recvfrom(sockfd, &rec_data, sizeof(rec_data), 0,(struct sockaddr *)&from, &from_len);
                    if (recv_size < 0)
                        break;
                    if (recv_size >= 4 && from.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                        from.sin_port == client_addr.sin_port && ntohs(rec_data.opcode) == DATA &&
                        ntohs(rec_data.body.data_packet.block_number) == block_number)
                        send_ack(sockfd, &client_addr, client_len, block_number, NULL);
                }
                tv.tv_sec = TIMEOUT_SEC;
                setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            }
            progress_done();
            return 0;
        }
        block_number++;
    }
}