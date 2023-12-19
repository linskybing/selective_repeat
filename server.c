#include "lab.h"
#include <pthread.h>
#include <unistd.h>

bool* flag = NULL;
pthread_t* send_thread = NULL;
pthread_mutex_t lock;

size_t base = 0;
size_t count = 0;


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

void* timeout(void* args) {
    Packet* send = (Packet*) args;
    while(!flag[send->header.seq]) {

        printf("Send SEQ = %u\n", send->header.seq);

        sendto(sockfd, send, sizeof(*send), 0, (struct sockaddr *)&clientInfo, sizeof(struct sockaddr_in));

        usleep(TIMEOUT * 100);
    } 

    return NULL;
}

void* handleAck() {
    Packet recv;
    while(base != count) {
        recvfrom(sockfd, &recv, sizeof(recv), 0, NULL, NULL);
        printf("Received ACK = %u\n", recv.header.ack);

        //critical section
        pthread_mutex_trylock(&lock);
        flag[recv.header.ack] = true;

        if (recv.header.ack == base) {
            for(int i = base; i < base + WINDOW_SIZE && flag[i]; i++, base++);
        }
        pthread_mutex_unlock(&lock);
    }

    return NULL;
}

void sendFile(FILE *fd) {
    size_t filesize = getFileSize(fd);

    size_t current = 0;
    count = filesize / 1024 + 1;

    Packet* sendBuffer = malloc(sizeof(Packet) * (count));
    send_thread = (pthread_t*) malloc(sizeof(pthread_t) * count);
    pthread_mutex_init(&lock, NULL);
    
    // ack record
    flag = malloc(sizeof(bool) * (count));
    memset(flag, false, count);

    unsigned int seq = 0;

    // create receive thread
    pthread_t recv_p;
    pthread_create(&recv_p, NULL, handleAck, NULL);

    while (current < filesize) {

        if (seq >= base + WINDOW_SIZE) {
            continue;
        }

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
        
        pthread_create(&send_thread[seq], NULL, timeout, &sendBuffer[seq]);

        current += sendBuffer[seq].header.size;
        seq++;

    }

    for (int i = 0; i < count; i++) {
        pthread_join(send_thread[i], NULL);
    }

    pthread_join(recv_p, NULL);
    // free
    free(sendBuffer);
    free(send_thread);
    free(flag);

    sendBuffer = NULL;
    send_thread = NULL;
    flag = NULL;
    base = 0;
    count = 0;
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
                sendFile(fd);
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