#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1  
#endif

#if USE_AESD_CHAR_DEVICE
#define FILE_PATH "/dev/aesdchar"
#else
#define FILE_PATH "/var/tmp/aesdsocketdata"
#endif

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
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/queue.h>


#define PORT             "9000"
#define BUFFER_SIZE      1024
#define TIMESTAMP_INTSEC 10     

static int    g_server_socket = -1;
static bool   g_exit_flag     = false; 
pthread_mutex_t g_mutex       = PTHREAD_MUTEX_INITIALIZER;

typedef struct client_thread_s 
{
    pthread_t thread_id;
    int client_fd;
    struct sockaddr_in client_addr;

    SLIST_ENTRY(client_thread_s) entries;
} client_thread_t;


SLIST_HEAD(slisthead, client_thread_s) g_thread_list_head = SLIST_HEAD_INITIALIZER(g_thread_list_head);


void* client_thread_func(void* thread_param);
void* timestamp_thread_func(void* arg);
void  cleanup_and_exit(int signum);
void  graceful_shutdown(void);
int   setup_server_socket(const char* port, int use_daemon);


void cleanup_and_exit(int signum)
{
    syslog(LOG_INFO, "Caught signal %d, requesting exit", signum);
    g_exit_flag = true;

    if(g_server_socket != -1)
        close(g_server_socket);
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
    	exit(EXIT_SUCCESS); // parent
    }
    
    if(setsid() < 0) 
    {
    	exit(EXIT_FAILURE);
    }
    
    signal(SIGHUP, SIG_IGN);
    pid = fork();
    
    if(pid < 0)
    {
    	exit(EXIT_FAILURE);
    }
    
    if(pid > 0) 
    {
    	exit(EXIT_SUCCESS); // parent
    }
    
    umask(0);
    chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}


int main(int argc, char *argv[])
{
    
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);

    int daemon_mode = (argc == 2 && strcmp(argv[1], "-d") == 0);
    if (daemon_mode) 
    {
        daemonize();
    }

#if !USE_AESD_CHAR_DEVICE
    int fd = open(FILE_PATH, O_CREAT|O_RDWR|O_TRUNC, 0666);
    if(fd < 0) 
    {
        syslog(LOG_ERR, "Failed to open/create %s: %s", FILE_PATH, strerror(errno));
        return EXIT_FAILURE;
    }
    close(fd);
#endif

#if !USE_AESD_CHAR_DEVICE
    pthread_t timer_thread;
    pthread_create(&timer_thread, NULL, timestamp_thread_func, NULL);
#endif

    g_server_socket = setup_server_socket(PORT, daemon_mode);
    if(g_server_socket < 0) 
    {
        syslog(LOG_ERR, "setup_server_socket failed");
        g_exit_flag = true;
    }

    while(!g_exit_flag)
    {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_fd = accept(g_server_socket, (struct sockaddr*)&client_addr, &addr_size);
        if(client_fd < 0)
        {
            if(errno == EINTR && g_exit_flag) break; 
            if(!g_exit_flag) 
            {
                syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            }
            continue;
        }

        client_thread_t *new_node = malloc(sizeof(client_thread_t));
        if(!new_node)
        {
            syslog(LOG_ERR, "malloc failed for client_thread");
            close(client_fd);
            continue;
        }
        new_node->client_fd = client_fd;
        new_node->client_addr = client_addr;

        SLIST_INSERT_HEAD(&g_thread_list_head, new_node, entries);

        int rc = pthread_create(&new_node->thread_id, NULL, client_thread_func, (void*)new_node);
        if(rc != 0) 
        {
            syslog(LOG_ERR, "pthread_create failed: %s", strerror(rc));
            SLIST_REMOVE(&g_thread_list_head, new_node, client_thread_s, entries);
            free(new_node);
            close(client_fd);
        }
    }

    graceful_shutdown();


#if !USE_AESD_CHAR_DEVICE
    pthread_join(timer_thread, NULL);
#endif

    pthread_mutex_destroy(&g_mutex);
    closelog();
    return 0;
}


void* client_thread_func(void* thread_param)
{
    client_thread_t *tinfo = (client_thread_t*)thread_param;

    char *ip_str = inet_ntoa(tinfo->client_addr.sin_addr);
    syslog(LOG_INFO, "Accepted connection from %s", ip_str);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    
    while(!g_exit_flag)
    {
        bytes_received = recv(tinfo->client_fd, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) 
        {
            break;
        }
        
        pthread_mutex_lock(&g_mutex);
        FILE *file = fopen(FILE_PATH, "a+");
        if(!file) 
        {
            syslog(LOG_ERR, "Failed to open %s for append", FILE_PATH);
            pthread_mutex_unlock(&g_mutex);
            break;
        }
        fwrite(buffer, 1, bytes_received, file);
        fflush(file);

        bool found_newline = (memchr(buffer, '\n', bytes_received) != NULL);
        if(found_newline) 
        {
            rewind(file);

            size_t nread;
            while((nread = fread(buffer, 1, BUFFER_SIZE, file)) > 0) 
            {
                send(tinfo->client_fd, buffer, nread, 0);
            }
        }
        fclose(file);
        pthread_mutex_unlock(&g_mutex);
    }

    shutdown(tinfo->client_fd, SHUT_RDWR);
    close(tinfo->client_fd);

    syslog(LOG_INFO, "Closed connection from %s", ip_str);

    pthread_mutex_lock(&g_mutex); 
    SLIST_REMOVE(&g_thread_list_head, tinfo, client_thread_s, entries);
    pthread_mutex_unlock(&g_mutex);

    free(tinfo);
    return NULL;
}

#if !USE_AESD_CHAR_DEVICE
void* timestamp_thread_func(void* arg)
{
    while(!g_exit_flag)
    {
        sleep(TIMESTAMP_INTSEC);
        if(g_exit_flag) break;

        time_t now = time(NULL);
        struct tm* tmp = localtime(&now);
        if(!tmp) continue;

        char timestr[128];
        strftime(timestr, sizeof(timestr), "timestamp:%a, %d %b %Y %T %z\n", tmp);

        pthread_mutex_lock(&g_mutex);
        FILE* file = fopen(FILE_PATH, "a");
        if(file) 
        {
            fputs(timestr, file);
            fclose(file);
        }
        pthread_mutex_unlock(&g_mutex);
    }
    return NULL;
}
#endif


void graceful_shutdown(void)
{
    if(g_server_socket != -1) 
    {
        close(g_server_socket);
        g_server_socket = -1;
    }

    client_thread_t *tnode = NULL;
    while(!SLIST_EMPTY(&g_thread_list_head))
    {
        tnode = SLIST_FIRST(&g_thread_list_head);
        pthread_join(tnode->thread_id, NULL);
    }
}

int setup_server_socket(const char* port, int use_daemon)
{
    struct addrinfo hints, *servinfo, *p;
    int sockfd, rc, yes=1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;       
    hints.ai_socktype = SOCK_STREAM;   
    hints.ai_flags    = AI_PASSIVE;    

    if((rc = getaddrinfo(NULL, port, &hints, &servinfo)) != 0)
    {
        syslog(LOG_ERR, "getaddrinfo failed: %s", gai_strerror(rc));
        return -1;
    }

    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if(sockfd < 0) 
        {
            continue;
        }
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if(bind(sockfd, p->ai_addr, p->ai_addrlen) == 0)
        {
            break;
        }
        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(servinfo);

    if(sockfd < 0)
    {
        syslog(LOG_ERR, "Could not bind socket");
        return -1;
    }

    if(listen(sockfd, SOMAXCONN) != 0)
    {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    syslog(LOG_INFO, "Listening on port %s", port);
    return sockfd;
}

