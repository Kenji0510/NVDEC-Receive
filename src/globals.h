#ifndef GLOBALS_H
#define GLOBALS_H

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

#define SHM_FOR_DATA_INFO_NAME "/shm_for_data_info"
#define SHM_FOR_SIZE_DATA_INFO 20   // 20 bytes
#define SHM_FOR_DATA_NAME "/shm_for_data"
#define SHM_FOR_SIZE_DATA 9331200  // (1920*1080*3 bytes) * 1.5 = 9331200 bytes

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t data_size;
} frame_data_info;

typedef struct {
    int argc;
    char** argv;
    void* shm_for_data_info;
    int shm_fd_for_data_info;
    void* shm_for_data;
    int shm_fd_for_data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    sem_t* sem_write;
    const char* sem_write_name;
    sem_t* sem_read;
    const char* sem_read_name;
    sem_t sem_write_thread;
    sem_t sem_read_thread;

    frame_data_info* frame_data_info;
    uint8_t* data;
} thread_args;

/*
extern void* shm_for_data_info;
extern int shm_fd_for_data_info;
extern void* shm_for_data;
extern int shm_fd_for_data;
extern pthread_mutex_t mutex;
extern pthread_cond_t cond;
extern sem_t* sem;
extern const char* sem_name;
*/

#ifdef __cplusplus
}
#endif

#endif // GLOBALS_H