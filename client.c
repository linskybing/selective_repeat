#include "lab.h"

Packet* packets = NULL;
bool* flag = NULL;
size_t count = 0;

void enterServerInfo(char *serverIP, unsigned short *serverPort) {
    printf("═══ Enter Server Info ═══\n");
    printf("Server IP: ");
    scanf("%s", serverIP);
    printf("Server port: ");
    scanf("%hu", serverPort);
    printf("═════════════════════════\n");
}

void sendRequest(char *command, char *filename) {
    Packet packet;
    memset(&packet, 0, sizeof(packet));

    // download<space>filename, without null terminator
    packet.header.size = strlen(command) + 1 + strlen(filename);
    packet.header.isLast = true;
    // Concatenate the command "download" with the filename
    snprintf((char *)packet.data, sizeof(packet.data) - 1, "%s %s", command, filename);

    if (sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&serverInfo, sizeof(struct sockaddr_in)) == -1) {
        perror("sendto()");
        exit(EXIT_FAILURE);
    }
}

void recvResponse(char *response) {
    Packet packet;
    memset(&packet, 0, sizeof(packet));

    if (recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&serverInfo, (socklen_t *)&addrlen) == -1) {
        perror("recvfrom()");
        exit(EXIT_FAILURE);
    }
    // Copy the packet data into the response buffer
    strncpy(response, (char *)packet.data, packet.header.size);
}

void sendAck(unsigned int ack) {
    Packet packet;
    memset(&packet, 0, sizeof(packet));

    packet.header.ack = ack;
    packet.header.isLast = true;

    if (sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&serverInfo, sizeof(struct sockaddr_in)) == -1) {
        perror("sendto()");
        exit(EXIT_FAILURE);
    }
}

bool isLoss(double p) {
    // Generate a random number between 0 and 1
    double r = (double)rand() / RAND_MAX;
    return r < p;
}

void recvFile(char *buffer) {
    // Keep track of the current sequence number
    Packet p;

    memset(&p, 0, sizeof(p));

    unsigned int base = 0;
    time_t start, end;
    start = time(NULL);
    while (true) {
        // Receive a packet first, then use isLoss()
        // to simulate if it has packet loss
        memset(&p, 0, sizeof(p));
        
        recvfrom(sockfd, &p, sizeof(p), 0, (struct sockaddr *)&serverInfo, (socklen_t *)&addrlen);
        // Simulate packet loss
        if (isLoss(LOSS_RATE)) {
            printf("Oops! Packet loss!\n");
            continue;
        }

        printf("Received SEQ = %u\n", p.header.seq);
        
        if (p.header.seq < base + WINDOW_SIZE){
            sendAck(p.header.seq);
            
            if (p.header.seq >= base) {
                packets[p.header.seq] = p;
                flag[p.header.seq] = true;
            }

            if (p.header.seq == base) {
                for (int i = base; flag[i]; i++, base++);
                if(base == count) break;
            }
        }
        printf("Base = %d\n", base);
        
    }

    int offset = 0;
    for (int i = 0; i < base; i++) {
        memcpy(buffer + offset, packets[i].data, packets[i].header.size);
        offset += packets[i].header.size;
    }

    end = time(NULL);
    printf("Elapsed: %ld sec\n", end - start);
}

void writeFile(char *buffer, unsigned int filesize, char *filename) {
    char newFilename[strlen("download_") + 64];  // filename[64]
    memset(newFilename, '\0', sizeof(newFilename));
    // Concatenate "download_" with the filename
    snprintf(newFilename, sizeof(newFilename) - 1, "download_%s", basename(filename));
    printf("Saving %s\n", newFilename);

    // ╔═══════════════════════════════════════════╗
    // ║ Please implement the following procedures ║
    // ╚═══════════════════════════════════════════╝

    // Create a file descriptor
    FILE *file;
    // Name the file as newFilename and open it in write-binary mode
    file = fopen(newFilename, "wb");
    // Write the buffer into the file
    fwrite(buffer, 1, filesize, file);
    // Close the file descriptor
    fclose(file);
    // Set the file descriptor to NULL
    file = NULL;
    printf("File has been written\n");
}

int main() {
    // Seed the random number generator for packet loss simulation
    srand(time(NULL));
    // xxx.xxx.xxx.xxx + null terminator
    char serverIP[16] = {'\0'};
    unsigned short serverPort;

    enterServerInfo(serverIP, &serverPort);
    setServerInfo(inet_addr(serverIP), serverPort);
    createSocket();

    char command[32];
    char filename[64];
    char response[64];
    size_t filesize;

    while (true) {
        memset(command, '\0', sizeof(command));
        memset(filename, '\0', sizeof(filename));
        memset(response, '\0', sizeof(response));
        filesize = 0;

        printf("Please enter a command:\n");
        if (scanf("%s", command) == EOF)
            break;

        if (strcmp(command, "exit") == 0)
            break;

        if (strcmp(command, "download") == 0) {
            scanf("%s", filename);
            // Send the download request
            sendRequest(command, filename);
            // Receive the response
            recvResponse(response);
            // Determine whether the file exists
            if (strcmp(response, "NOT_FOUND") == 0) {
                printf("File does not exist\n");
                continue;
            }
            // Ignore characters before "=", then read the file size
            sscanf(response, "%*[^=]=%zu", &filesize);
            printf("File size is %zu bytes\n", filesize);
            // Allocate a buffer with the file size
            char *buffer = malloc(filesize);
            count = filesize / 1024 + 1;
            packets = malloc(count * sizeof(Packet));
            flag = malloc(count * sizeof(bool));
            memset(flag, false, count);
            printf("═══════ Receiving ═══════\n");
            recvFile(buffer);
            printf("═════════════════════════\n");
            writeFile(buffer, filesize, filename);
            printf("═════════════════════════\n");

            free(buffer);
            buffer = NULL;
            continue;
        }
        printf("Invalid command\n");
        // Clear the input buffer following the invalid command
        while (getchar() != '\n')
            continue;
    }
    close(sockfd);
    return 0;
}