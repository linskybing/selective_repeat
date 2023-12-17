#include "lab.h"
#include <pthread.h>
#include <unistd.h>

bool* flag = NULL;
int base = 0;
bool run = true;

pthread_mutex_t sendMutex = PTHREAD_MUTEX_INITIALIZER;

void printServerInfo(unsigned short port) {
    printf("═══════ Server ═══════\n");
    printf("Server IP is 127.0.0.1\n");
    printf("Listening on port %hu\n", port);
    printf("══════════════════════\n");
}

void sendMessage(char *message) {
    Packet packet;
    memset(&packet, 0, sizeof(packet));

    packet.header.size = strlen(message);
    packet.header.isLast = true;
    strcpy((char *)packet.data, message);

    if (sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&clientInfo, sizeof(struct sockaddr_in)) == -1) {
        perror("sendto()");
        exit(EXIT_FAILURE);
    }
}

void recvCommand(char *command) {
    Packet packet;
    memset(&packet, 0, sizeof(packet));

    if (recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&clientInfo, (socklen_t *)&addrlen) == -1) {
        perror("recvfrom()");
        exit(EXIT_FAILURE);
    }

    strncpy(command, (char *)packet.data, packet.header.size);
}

FILE *getFile(char *filename) {
    FILE *fd = fopen(filename, "rb");
    return fd;
}

size_t getFileSize(FILE *fd) {
    fseek(fd, 0, SEEK_END);
    size_t size = ftell(fd);
    return size;
}

void* timeout(Packet send) {
    while(1) {
        printf("Send SEQ = %u\n", send.header.seq);

        sendto(sockfd, &send, sizeof(send), 0, (struct sockaddr *)&clientInfo, sizeof(struct sockaddr_in));

        usleep(TIMEOUT*1000);

        if (flag[send.header.seq]) return NULL;
    } 

    return NULL;
}

void* handleAck() {
    Packet recv;
    while(run) {

        recvfrom(sockfd, &recv, sizeof(recv), 0, NULL, NULL);
        printf("Received ACK = %u\n", recv.header.ack);

        //critical section
        pthread_mutex_lock(&sendMutex);

        flag[recv.header.ack] = true;

        if (recv.header.ack == base) {
            for(int i = base; i < base + WINDOW_SIZE && flag[i]; i++, base++);
        }

        printf("Base = %d\n", base);
        pthread_mutex_unlock(&sendMutex);
    }
    return NULL;
}

void* sendFile(FILE *fd) {
    size_t filesize = getFileSize(fd);
    size_t current = 0;
    const size_t count = filesize / 1024 + 1;
    Packet* sendBuffer = malloc(sizeof(Packet) * (count));
    flag = malloc(sizeof(bool) * (count));
    memset(flag, false, count);

    unsigned int seq = 0;
    while (current < filesize) {

        // critical section
        pthread_mutex_lock(&sendMutex);
        if (seq >= base + WINDOW_SIZE) {
            pthread_mutex_unlock(&sendMutex);
            continue;
        }
        pthread_mutex_unlock(&sendMutex);

        fseek(fd, current, SEEK_SET);
        fread(sendBuffer[seq].data, 1, 1024, fd);

        sendBuffer[seq].header.seq = seq;

        if (ftell(fd) == filesize) {
            sendBuffer[seq].header.isLast = true;
            sendBuffer[seq].header.size = ftell(fd) - current;
        }
        else {
            sendBuffer[seq].header.size = 1024;
        }

        // create a thread
        pthread_t* t = (pthread_t*) malloc(sizeof(pthread_t));

        pthread_create(t, NULL, timeout, &sendBuffer[seq]);
        pthread_detach(*t);

        current += sendBuffer[seq].header.size;
        seq++;

    }

    run = false;
    return NULL;
}

int main(int argc, char **argv) {

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    unsigned short port = atoi(argv[1]);

    setServerInfo(INADDR_ANY, port);
    printServerInfo(port);
    setClientInfo();
    createSocket();
    bindSocket();

    FILE *fd;
    char command[96];
    char message[64];

    while (true) {
        memset(command, '\0', sizeof(command));
        memset(message, '\0', sizeof(message));

        printf("Server is waiting...\n");
        recvCommand(command);

        printf("Processing command...\n");
        char *str = strtok(command, " ");

        if (strcmp(str, "download") == 0) {
            str = strtok(NULL, "");
            printf("Filename is %s\n", str);

            if ((fd = getFile(str))) {
                snprintf(message, sizeof(message) - 1, "FILE_SIZE=%zu", getFileSize(fd));
                sendMessage(message);

                printf("══════ Sending ═══════\n");
                pthread_t send_p, recv_p;
                pthread_create(&send_p, NULL, sendFile, fd);
                pthread_create(&recv_p, NULL, handleAck, NULL);
                pthread_join(send_p, NULL);
                pthread_join(recv_p, NULL);

                printf("══════════════════════\n");

                fclose(fd);
                fd = NULL;
            } else {
                printf("File does not exist\n");
                sendMessage("NOT_FOUND");
            }
            continue;
        }
        printf("Invalid command\n");
    }
    return 0;
}