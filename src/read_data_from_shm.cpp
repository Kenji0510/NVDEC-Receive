#include "read_data_from_shm.hpp"

void* read_data_from_shm(void* args) {
    thread_args* thd_args = (thread_args*) args;
    size_t total_data_size = 0;

    while (true) {
        sem_wait(thd_args->sem_read);
        pthread_mutex_lock(&thd_args->mutex);

        // Copy data from shared memory
        memcpy(thd_args->frame_data_info, thd_args->shm_for_data_info, sizeof(frame_data_info));
        memcpy(thd_args->data, thd_args->shm_for_data, thd_args->frame_data_info->data_size);

        std::cout << "Data size: " << thd_args->frame_data_info->data_size << std::endl;
        std::cout << "Data[10]: " << (int)thd_args->data[10] << std::endl;
        total_data_size += thd_args->frame_data_info->data_size;

        //pthread_cond_signal(&thd_args->cond);
        pthread_mutex_unlock(&thd_args->mutex);
    
        sem_post(thd_args->sem_write);

        sem_post(&(thd_args->sem_write_thread));
        sem_wait(&(thd_args->sem_read_thread));

        //usleep(50000);
        
        if (thd_args->frame_data_info->data_size == 999999) {
            sem_post(&(thd_args->sem_write_thread));
            std::cout << "End of file" << std::endl;
            std::cout << "Total data size: " << total_data_size << std::endl;
            break;
        }
    }

    return NULL;
}