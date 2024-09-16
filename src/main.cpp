#include <iostream>
#include <csignal>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "globals.h"
#include "AppDec.hpp"
#include "read_data_from_shm.hpp"

void* nvdec(void* args);

pthread_t nvdec_thread;
pthread_t shm_thread;

// For siganl handler
thread_args* args_for_signal_handler;

void cleanup_shared_memory(int signum) {
    std::cout << "\e[33m" << "Cleaning up shared memory" << "\e[0m" << std::endl;

    pthread_cancel(nvdec_thread);
    pthread_cancel(shm_thread);
    pthread_join(nvdec_thread, NULL);
    pthread_join(shm_thread, NULL);

    free(args_for_signal_handler->frame_data_info);
    free(args_for_signal_handler->data);

    sem_destroy(&(args_for_signal_handler->sem_write_thread));
    sem_destroy(&(args_for_signal_handler->sem_read_thread));

    if (munmap(args_for_signal_handler->shm_for_data_info, SHM_FOR_SIZE_DATA_INFO) == -1) {
        std::cerr << "munmap failed for shm_for_data_info" << std::endl;
        exit(1);
    }
    /*
    if (shm_unlink(SHM_FOR_DATA_INFO_NAME) == -1) {
        std::cerr << "shm_unlink failed for shm_for_data_info" << std::endl;
        exit(1);
    }
    */
    if (munmap(args_for_signal_handler->shm_for_data, SHM_FOR_SIZE_DATA) == -1) {
        std::cerr << "munmap failed for shm_for_data" << std::endl;
        exit(1);
    }
    /*
    if (shm_unlink(SHM_FOR_DATA_NAME) == -1) {
        std::cerr << "shm_unlink failed for shm_for_data" << std::endl;
        exit(1);
    }
    */

    if (sem_close(args_for_signal_handler->sem_write) == -1) {
        std::cerr << "Failed to close semaphore" << std::endl;
    }
    /*
    if (sem_unlink(args_for_signal_handler->sem_name) == -1) {
        std::cerr << "Failed to unlink semaphore" << std::endl;
    }
    */
    if (sem_close(args_for_signal_handler->sem_read) == -1) {
        std::cerr << "Failed to close semaphore" << std::endl;
    }
    /*
    if (sem_unlink(args_for_signal_handler->sem_read_name) == -1) {
        std::cerr << "Failed to unlink semaphore" << std::endl;
    }
    */
}

void setup_shared_memory(thread_args* args) {
    args->shm_fd_for_data_info = shm_open(SHM_FOR_DATA_INFO_NAME, O_CREAT | O_RDWR, 0666);
    if (args->shm_fd_for_data_info == -1) {
        std::cerr << "shm_open failed for shm_fd_for_data_info" << std::endl;
        exit(1);
    }
    /*
    if (ftruncate(args->shm_fd_for_data_info, SHM_FOR_SIZE_DATA_INFO) == -1) {
        std::cerr << "ftruncate failed for shm_fd_for_data_info" << std::endl;
        exit(1);
    }
    */
    args->shm_for_data_info = mmap(0, SHM_FOR_SIZE_DATA_INFO, PROT_READ | PROT_WRITE, MAP_SHARED, args->shm_fd_for_data_info, 0);
    if (args->shm_for_data_info == MAP_FAILED) {
        std::cerr << "mmap failed for shm_for_data_info" << std::endl;
        exit(1);
    }

    args->shm_fd_for_data = shm_open(SHM_FOR_DATA_NAME, O_CREAT | O_RDWR, 0666);
    if (args->shm_fd_for_data == -1) {
        std::cerr << "shm_open failed for shm_fd_for_data" << std::endl;
        exit(1);
    }
    /*
    if (ftruncate(args->shm_fd_for_data, SHM_FOR_SIZE_DATA) == -1) {
        std::cerr << "ftruncate failed for shm_fd_for_data" << std::endl;
        exit(1);
    }
    */
    args->shm_for_data = mmap(0, SHM_FOR_SIZE_DATA, PROT_READ | PROT_WRITE, MAP_SHARED, args->shm_fd_for_data, 0);
    if (args->shm_for_data == MAP_FAILED) {
        std::cerr << "mmap failed for shm_for_data" << std::endl;
        exit(1);
    }

    args->sem_write = sem_open(args->sem_write_name, O_CREAT, 0666, 1);
    if (args->sem_write == SEM_FAILED) {
        std::cerr << "sem_write_open failed" << std::endl;
        munmap(args->shm_for_data_info, SHM_FOR_SIZE_DATA_INFO);
        close(args->shm_fd_for_data_info);
        munmap(args->shm_for_data, SHM_FOR_SIZE_DATA);
        close(args->shm_fd_for_data);
    }

    args->sem_read = sem_open(args->sem_read_name, O_CREAT, 0666, 1);
    if (args->sem_read == SEM_FAILED) {
        std::cerr << "sem_read_open failed" << std::endl;
        munmap(args->shm_for_data_info, SHM_FOR_SIZE_DATA_INFO);
        close(args->shm_fd_for_data_info);
        munmap(args->shm_for_data, SHM_FOR_SIZE_DATA);
        close(args->shm_fd_for_data);
    }

    if (pthread_mutex_init(&(args->mutex), NULL) != 0) {
        std::cerr << "Failed to initialize mutex" << std::endl;
    }
    if (pthread_cond_init(&(args->cond), NULL) != 0) {
        std::cerr << "Failed to initialize condition variable" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, cleanup_shared_memory);

    thread_args args = {
        .argc = argc,
        .argv = argv,
        .shm_for_data_info = NULL,
        .shm_fd_for_data_info = -1,
        .shm_for_data = NULL,
        .shm_fd_for_data = -1,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .sem_write = NULL,
        .sem_write_name = "/sem_write",
        .sem_read = NULL,
        .sem_read_name = "/sem_read",
        .frame_data_info = NULL,
        .data = NULL
    };

    // For siganl handler
    args_for_signal_handler = &args;

    setup_shared_memory(&args);

    sem_init(&(args.sem_write_thread), 0, 0);
    sem_init(&(args.sem_read_thread), 0, 0);

    frame_data_info* data_info = (frame_data_info*)malloc(sizeof(frame_data_info));
    uint8_t* data = (uint8_t*)malloc(SHM_FOR_SIZE_DATA);

    args.frame_data_info = data_info;
    args.data = data;
    
    pthread_create(&nvdec_thread, NULL, nvdec, &args);
    pthread_create(&shm_thread, NULL, read_data_from_shm, &args);

    while(1) {
        sleep(1);
    }
   
    cleanup_shared_memory(0);
    return 0;
}