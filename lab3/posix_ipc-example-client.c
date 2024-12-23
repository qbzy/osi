#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>

// Переиспользуем те же константы
#define SHM_SIZE 4096

// имена семафоров (должны совпадать с серверными)
#define SEM_CAN_WRITE_DATA   "/posix_ipc_example_can_write_data"
#define SEM_CAN_READ_DATA    "/posix_ipc_example_can_read_data"

#define SEM_CAN_WRITE_ERR    "/posix_ipc_example_can_write_err"
#define SEM_CAN_READ_ERR     "/posix_ipc_example_can_read_err"

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <filename> <shm_data_name> <shm_err_name>\n", argv[0]);
        return 1;
    }

    char *filename     = argv[1];
    char *shm_data_nm  = argv[2];
    char *shm_err_nm   = argv[3];

    // 1) открываем файл
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0600);
    if (fd == -1) {
        perror("open file");
        return 1;
    }

    // 2) открываем разделяемую память data
    int shm_fd_data = shm_open(shm_data_nm, O_RDWR, 0666);
    if (shm_fd_data == -1) {
        perror("shm_open data");
        close(fd);
        return 1;
    }
    char *shm_data = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_data, 0);
    if (shm_data == MAP_FAILED) {
        perror("mmap data");
        close(fd);
        return 1;
    }

    // 3) открываем разделяемую память err
    int shm_fd_err = shm_open(shm_err_nm, O_RDWR, 0666);
    if (shm_fd_err == -1) {
        perror("shm_open err");
        munmap(shm_data, SHM_SIZE);
        close(fd);
        return 1;
    }
    char *shm_err = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_err, 0);
    if (shm_err == MAP_FAILED) {
        perror("mmap err");
        munmap(shm_data, SHM_SIZE);
        close(fd);
        return 1;
    }

    // 4) открываем семафоры
    sem_t* sem_can_write_data = sem_open(SEM_CAN_WRITE_DATA, 0);
    sem_t* sem_can_read_data  = sem_open(SEM_CAN_READ_DATA, 0);
    sem_t* sem_can_write_err  = sem_open(SEM_CAN_WRITE_ERR, 0);
    sem_t* sem_can_read_err   = sem_open(SEM_CAN_READ_ERR, 0);

    if (sem_can_write_data == SEM_FAILED || sem_can_read_data == SEM_FAILED
        || sem_can_write_err == SEM_FAILED || sem_can_read_err == SEM_FAILED)
    {
        perror("sem_open in child");
        // чистим ресурсы...
        return 1;
    }

    printf("[Child] started, ready to read from shm_data...\n");

    // Основной цикл: читаем команду из shm_data, проверяем, пишем в файл/ошибку
    while (1) {
        sem_wait(sem_can_read_data); // ждём, пока родитель что-то запишет
        if (shm_data[0] == '\0') {
            // пустая строка => родитель закончил ввод
            break;
        }

        // копируем строку локально
        char buf[4096];
        strncpy(buf, shm_data, sizeof(buf)-1);
        buf[sizeof(buf)-1] = '\0';

        // Проверяем, заканчивается ли строка '.' или ';'
        size_t length = strlen(buf);
        if (length == 0) {
            // совсем пустая, просто игнорируем
        } else {
            char last = buf[length-1];
            if (last != '.' && last != ';') {
                // ОШИБКА! => пишем её в shm_err, чтобы родитель это увидел
                sem_wait(sem_can_write_err);  // ждём, когда можно писать ошибку
                snprintf(shm_err, SHM_SIZE,
                         "child error: string does not end with '.' or ';'. The string was: %s",
                         buf);
                sem_post(sem_can_read_err);
            } else {
                // Добавим \n и пишем в файл
                strcat(buf, "\n");
                ssize_t written = write(fd, buf, strlen(buf));
                if (written < 0) {
                    // Ошибка записи в файл — тоже сообщим родителю
                    sem_wait(sem_can_write_err);
                    snprintf(shm_err, SHM_SIZE,
                             "child error: write to file failed, errno=%d\n", errno);
                    sem_post(sem_can_read_err);
                }
            }
        }

        // Освобождаем буфер shm_data
        sem_post(sem_can_write_data);
    }

    // Закрываемся
    printf("[Child] finishing.\n");

    // Освобождаем ресурсы
    sem_close(sem_can_write_data);
    sem_close(sem_can_read_data);
    sem_close(sem_can_write_err);
    sem_close(sem_can_read_err);

    munmap(shm_data, SHM_SIZE);
    munmap(shm_err, SHM_SIZE);
    close(fd);

    return 0;
}
