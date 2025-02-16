#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <syslog.h>
#include <signal.h>
#include <sys/stat.h>


#define PORT "9000"
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int sockfd = -1;
int clientfd = -1;

void cleanup_and_exit(int signum) 
{
    syslog(LOG_INFO, "Caught signal, exiting");

    if (clientfd != -1) 
    {
        close(clientfd);
    }
    if (sockfd != -1) 
    {
        close(sockfd);
    }
    
    remove(FILE_PATH);
    closelog();
    exit(0);
}

void handle_client(int clientfd, struct sockaddr_in *client_addr) 
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    FILE *file = fopen(FILE_PATH, "a+");

    if (!file) 
    {
        syslog(LOG_ERR, "Failed to open file");
        return;
    }

    syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr->sin_addr));

    while ((bytes_received = recv(clientfd, buffer, BUFFER_SIZE, 0)) > 0) 
    {
        fwrite(buffer, 1, bytes_received, file);
        fflush(file);

        if (memchr(buffer, '\n', bytes_received)) 
        {
            rewind(file);
            while ((bytes_received = fread(buffer, 1, BUFFER_SIZE, file)) > 0) 
            {
                send(clientfd, buffer, bytes_received, 0);
            }
            rewind(file);
        }
    }

    syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr->sin_addr));
    fclose(file);
}

void daemonize() 
{
    pid_t pid = fork();
    if (pid < 0) 
    {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) 
    {
        exit(EXIT_SUCCESS);
    }
    
    if (setsid() < 0) 
    {
        exit(EXIT_FAILURE);
    }
    
    signal(SIGHUP, SIG_IGN);
    pid = fork();
    
    if (pid < 0) 
    {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) 
    {
        exit(EXIT_SUCCESS);
    }

    umask(0);
    chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int main(int argc, char *argv[]) {
    struct addrinfo hints, *servinfo;
    struct sockaddr_in client_addr;
    socklen_t addr_size = sizeof(client_addr);
    int daemon_mode = (argc == 2 && strcmp(argv[1], "-d") == 0);

    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &servinfo) != 0) 
    {
        syslog(LOG_ERR, "getaddrinfo failed");
        return -1;
    }

    sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sockfd == -1) 
    {
        syslog(LOG_ERR, "socket creation failed");
        freeaddrinfo(servinfo);
        return -1;
    }

    int optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1) 
    {
        syslog(LOG_ERR, "bind failed");
        freeaddrinfo(servinfo);
        close(sockfd);
        return -1;
    }
    freeaddrinfo(servinfo);

    if (daemon_mode) 
    {
        daemonize();
    }

    if (listen(sockfd, SOMAXCONN) == -1) 
    {
        syslog(LOG_ERR, "listen failed");
        close(sockfd);
        return -1;
    }

    while (1) 
    {
        clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &addr_size);
        if (clientfd == -1) 
        {
            syslog(LOG_ERR, "accept failed");
            continue;
        }
        handle_client(clientfd, &client_addr);
        close(clientfd);
        clientfd = -1;
    }

    cleanup_and_exit(0);
}
