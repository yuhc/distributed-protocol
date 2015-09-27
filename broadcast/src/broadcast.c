#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#define NUM_PROC       5
#define NUM_EVENT    100
#define BASE_PORT   9000
#define BUFFER_SIZE 1024

typedef struct Event {
    int sender_id;
    int send_time, rec_time;
} Event;

typedef struct VectorClock {
    int sender_id;
    int rec_time;
    int vc[NUM_PROC];
} VClock, Message;

int       proc_id;
char      *proc_ip;

int       start_server_socket(const char *ip);
int       start_client_socket(const int port, const char *ip);
void      *server(void *arg);
void      *client(void *arg);

char      config_filename[] = "./data/broadcast.conf";
char      logs_filename[]   = "./data/broadcast.log";
FILE      *ifp, *ofp;

int       **delay;
struct
timespec  nanodelay, tim;

int       serv_listenfd;
pthread_t serv_tid, clnt_tid;
time_t    start_time, current_time;
int       ticks;
int       num_event = 0;
Event     events[NUM_EVENT];

VClock    vclock;

void signal_callback_handler(int signum) {
    printf("p%d catches signal %d\n", proc_id, signum);
    close(serv_listenfd);
    exit(signum);
}

int main (int argc, char *argv[]) { // id, ip
    if (argc != 3) {
        fprintf(stderr, "Argment error: broadcast proc_id proc_ip\n");
        return 1;
    }
    proc_id = atoi(argv[1]);
    proc_ip = argv[2];

    // register signal and signal handler
    signal(SIGINT, signal_callback_handler);

    // open file I/O
    ifp = fopen(config_filename, "r");
    if (ifp == NULL) {
        fprintf(stderr, "Can't open configuration file %s!\n", config_filename);
        return 1;
    }
    ofp = fopen(logs_filename, "a");
    if (ofp == NULL) {
        fprintf(stderr, "Can't write in log file %s!\n", logs_filename);
        fclose(ifp);
        return 1;
    }

    // read configuration
    delay = (int**) malloc(NUM_PROC*sizeof(int*));
    int i, j;
    for (i = 0; i < NUM_PROC; i++) {
        delay[i] = (int*) malloc(NUM_PROC*sizeof(int));
        for (j = 0; j < NUM_PROC; j++)
            fscanf(ifp, "%d", &delay[i][j]);
    }

    char  instruction[20]; // ignore "bc at" here
    char  comma;
    int   sender_id, event_time;
    while (fscanf(ifp, "%d%s%s", &sender_id, instruction, instruction) != EOF) {
        events[num_event].sender_id = sender_id;
        fscanf(ifp, "%d%c", &event_time, &comma);
        events[num_event++].send_time = event_time;
        while (comma == ',') {
            fscanf(ifp, "%d%c", &event_time, &comma);
            events[num_event].sender_id = sender_id;
            events[num_event++].send_time = event_time;
        }
    }
    fclose(ifp);

    // create threads
    sleep(1);  // wait for script
    time(&start_time);
    time(&current_time);
    ticks = difftime(start_time, current_time);
    fprintf(ofp, "%3d\t p%d\t SRT\t   NULL\n", ticks, proc_id);
    fclose(ofp);
    memset(vclock.vc, 0, sizeof(vclock.vc));
    vclock.sender_id = proc_id;

    nanodelay.tv_sec = 0; nanodelay.tv_nsec = 1e8;

    // start server
    serv_listenfd = start_server_socket(proc_ip);

    // start client
    int err = pthread_create(&clnt_tid, NULL, &client, NULL);
    if (err) {
        fprintf(stderr, "Client thread created failed\n");
        return 1;
    }
    fprintf(stderr, "Server %d starts\n", proc_id);

    // build connection
    int       connfd;
    struct    sockaddr_storage serv_storage;
    socklen_t serv_addr_size = sizeof(serv_storage);

    while ((connfd = accept(serv_listenfd, (struct sockaddr*) &serv_storage, &serv_addr_size))) {
        fprintf(stderr, "p%d builds a new connection\n", proc_id);
        err = pthread_create(&serv_tid, NULL, &server, (void*) &connfd);
        if (err) {
            fprintf(stderr, "Server thread created failed\n");
            return 1;
        }
        nanosleep(&nanodelay, &tim);
    }

    pthread_join(clnt_tid, NULL);

    return 0;
}

int start_server_socket(const char *ip) {
    int    listenfd = 0;
    struct sockaddr_in serv_addr;

    if ((listenfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        fprintf(stderr, "Open socket failed\n");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(BASE_PORT+proc_id);
    memset(serv_addr.sin_zero, '\0', sizeof(serv_addr.sin_zero));

    bind(listenfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
    if (listen(listenfd, NUM_PROC))
        fprintf(stderr, "Bind server socket error\n");

    return listenfd;
}

int start_client_socket(const int port, const char *ip) {
    int sockfd = 0;
    struct sockaddr_in serv_addr;

    sockfd = socket(PF_INET, SOCK_STREAM, 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(BASE_PORT+port);
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    memset(serv_addr.sin_zero, '\0', sizeof(serv_addr.sin_zero));

    while ((connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr))) != 0);

    return sockfd;
}

void* server(void *arg) {
    FILE *ofp = fopen(logs_filename, "a");
    if (ofp == NULL) {
        fprintf(stderr, "Server p%d can't write in log file %s!\n", proc_id, logs_filename);
        return NULL;
    }

    int       connfd = *(int*) arg;

    VClock    recv_buff[BUFFER_SIZE];
    int       buff_num = 0;
    Message   mes;
    int       i, j, delay_next, flag;

    if(fcntl(connfd, F_GETFL) & O_NONBLOCK)
        fprintf(stderr, "p%d is in non-blocking mode\n", proc_id);
    else {
        if(fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL) | O_NONBLOCK) < 0)
            fprintf(stderr, "p%d switches to non-blocking mode failed\n", proc_id);
        fprintf(stderr, "p%d switches to non-blocking mode\n", proc_id);
    }

    while (1) {
        if (recv(connfd, &mes, 1024, 0) > 0) {
            fprintf(stderr, "p%d receives a message from %d at %d whose rec_time=%d; mes vc=[", proc_id, mes.sender_id, ticks, mes.rec_time);
            for (i = 0; i < NUM_PROC; i++) fprintf(stderr, "%d%c", mes.vc[i], i==NUM_PROC-1?']':' '); fprintf(stderr, "; local vc=[");
            for (i = 0; i < NUM_PROC; i++) fprintf(stderr, "%d%c", vclock.vc[i], i==NUM_PROC-1?']':' '); fprintf(stderr, "\n");
            recv_buff[buff_num++] = mes;
            nanosleep(&nanodelay, &tim);
        }

        for (i = 0; i < buff_num; i++)
            if (recv_buff[i].rec_time <= (int)ticks && recv_buff[i].rec_time >= 0) {
                fprintf(stderr, "p%d receives a message from %d at %d\n", proc_id, recv_buff[i].sender_id, ticks);
                fprintf(ofp, "%3d\t p%d\t REC\t   %d:%d\n", ticks, proc_id, recv_buff[i].sender_id, recv_buff[i].vc[recv_buff[i].sender_id]);
                fflush(ofp);
                recv_buff[i].rec_time = -1;
            }

        delay_next = 1;
        while (delay_next) {
            delay_next = 0;
            for (i = 0; i < buff_num; i++) {
                mes = recv_buff[i];
                if (mes.rec_time < 0 && (vclock.vc[mes.sender_id] == mes.vc[mes.sender_id]-1 || mes.sender_id == proc_id)) {
                    delay_next = 1;
                    flag = 0;
                    for (j = 0; j < NUM_PROC; j++)
                        if (j != mes.sender_id && vclock.vc[j] < mes.vc[j]) flag = 1;
                    if (!flag) {
                        fprintf(stderr, "p%d delivers a message from %d at %d\n", proc_id, mes.sender_id, ticks);
                        fprintf(ofp, "%3d\t p%d\t DLR\t   %d:%d\n", ticks, proc_id, mes.sender_id, mes.vc[mes.sender_id]);
                        fflush(ofp);
                        recv_buff[i].rec_time = INT_MAX;
                        vclock.vc[mes.sender_id] = mes.vc[mes.sender_id];
                    }
                }
            }
        }

        nanosleep(&nanodelay, &tim);
    }

    fclose(ofp);

    return NULL;
}

void* client(void *arg) {
    FILE *ofp = fopen(logs_filename, "a");
    if (ofp == NULL) {
        fprintf(stderr, "Client p%d can't write in log file %s!\n", proc_id, logs_filename);
        return NULL;
    }

    sleep(2); // wait for server
 
    int clnt_sockfd[NUM_PROC];
    int i, j, k;
    for (i = 0; i < NUM_PROC; i++) {
        /*if (i != proc_id)*/ clnt_sockfd[i] = start_client_socket(i, proc_ip);
        sleep(1);
    }
    sleep(5);
    fprintf(stderr, "p%d finishes configuration\n", proc_id);
    time(&start_time); // retime

    while (1) {
        time(&current_time);
        ticks = difftime(current_time, start_time);
        for (j = 0; j < num_event; j++)
            if (events[j].sender_id == proc_id && events[j].send_time == (int)ticks) {
                vclock.vc[proc_id]++;     // update local Vector Clock
                fprintf(stderr, "p%d broadcasts message at %d with rec_time=%d; mes vc=[", proc_id, ticks, vclock.rec_time);
                for (k = 0; k < NUM_PROC; k++) fprintf(stderr, "%d%c", vclock.vc[k], k==NUM_PROC-1?']':' '); fprintf(stderr, "\n");
                fprintf(ofp, "%3d\t p%d\t BRC\t   %d:%d\n", ticks, proc_id, proc_id, vclock.vc[proc_id]);
                for (i = 0; i < NUM_PROC; i++)
                    /*  if (i != proc_id) */{
                        vclock.rec_time = events[j].send_time + delay[proc_id][i];
                        fprintf(stderr, "> p%d sends message to p%d at %d with rec_time=%d; mes vc=[", proc_id, i, ticks, vclock.rec_time);
                        for (k = 0; k < NUM_PROC; k++) fprintf(stderr, "%d%c", vclock.vc[k], k==NUM_PROC-1?']':' '); fprintf(stderr, "\n");
                        //fprintf(ofp, "%3d\t p%d\t SED\t   %d:%d\n", ticks, proc_id, proc_id, vclock.vc[proc_id]);
                        fflush(ofp);
                        send(clnt_sockfd[i], &vclock, sizeof(vclock), 0);
                    }
                events[j].send_time = -1; // message sent
            }

        nanosleep(&nanodelay, &tim);
    }

    fclose(ofp);

    return NULL;
}
