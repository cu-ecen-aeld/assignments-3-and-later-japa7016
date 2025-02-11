#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data *thread_func_args = (struct thread_data *)thread_param;

    usleep((thread_func_args->wait_to_obtain_ms * 1000));
    
    int ret;
    ret = pthread_mutex_lock(thread_func_args->mutex);
    if(ret != 0)
    {
        thread_func_args->thread_complete_success = false;
        perror("Mutex Locking failed!!");
        return (void *)thread_func_args;
    }

    usleep((thread_func_args->wait_to_release_ms * 1000));

    ret = pthread_mutex_unlock(thread_func_args->mutex);
    if(ret != 0)
    {
        thread_func_args->thread_complete_success = false;
        perror("Mutex Unlocking failed!!");
        return (void *)thread_func_args;
    }

    thread_func_args->thread_complete_success = true;

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    return (void *)thread_func_args;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data *data = (struct thread_data *)malloc(sizeof(struct thread_data));
    if(data == NULL)
    {
        perror("Memory Allocation Failed!!");
        return false;
    }

    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->thread_complete_success = false;

    int ret;
    ret = pthread_create(thread, NULL, threadfunc, (void *)data);
    if(ret != 0)
    {
        free(data);
        return false;
    }
    return true;
}

