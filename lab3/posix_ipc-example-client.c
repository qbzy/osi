#include <stdlib.h>      // для exit()
#include <stdint.h>
#include <fcntl.h>       // для open()
#include <unistd.h>      // для write(), read(), close(), _exit()
#include <sys/mman.h>    // для mmap(), munmap()
#include <sys/stat.h>    // для shm_open()
#include <sys/types.h>   // для pid_t
#include <string.h>      // для strlen(), strcpy(), strcat(), memset() и т.д.
#include <semaphore.h>   // для sem_open(), sem_wait() и т.д.
#include <errno.h>       // для errno

// ----------------------------------------------
// Общие параметры для data-шм
// ----------------------------------------------
#define SHM_DATA_NAME         "/posix_ipc_example_data"
#define SEM_CAN_WRITE_DATA    "/posix_ipc_example_can_write_data"
#define SEM_CAN_READ_DATA     "/posix_ipc_example_can_read_data"

// ----------------------------------------------
// Общие параметры для err-шм
// ----------------------------------------------
#define SHM_ERR_NAME          "/posix_ipc_example_err"
#define SEM_CAN_WRITE_ERR     "/posix_ipc_example_can_write_err"
#define SEM_CAN_READ_ERR      "/posix_ipc_example_can_read_err"

// ----------------------------------------------
#define SHM_SIZE 4096

//------------------------------------------------------------------------------
// Функция для вывода C-строки (null-terminated) в указанный дескриптор
static void write_str_to_fd(int fd, const char *s)
{
    if (s) {
        write(fd, s, strlen(s));
    }
}

//------------------------------------------------------------------------------
// Упрощённая функция вместо perror — выводит указанное сообщение + "\n"
static void simple_perror(const char *msg)
{
    write_str_to_fd(STDERR_FILENO, msg);
    write_str_to_fd(STDERR_FILENO, "\n");
}

//------------------------------------------------------------------------------
// Завершение с очисткой ресурсов
static void cleanup_and_exit(
        int fd_file,
        char *shm_data,
        char *shm_err,
        sem_t *sem_can_write_data,
        sem_t *sem_can_read_data,
        sem_t *sem_can_write_err,
        sem_t *sem_can_read_err,
        int exit_code
) {
    // Закрываем файл
    if (fd_file >= 0) {
        close(fd_file);
    }
    // Отключаем shm_data
    if (shm_data && shm_data != MAP_FAILED) {
        munmap(shm_data, SHM_SIZE);
    }
    // Отключаем shm_err
    if (shm_err && shm_err != MAP_FAILED) {
        munmap(shm_err, SHM_SIZE);
    }
    // Закрываем семафоры
    if (sem_can_write_data && sem_can_write_data != SEM_FAILED) {
        sem_close(sem_can_write_data);
    }
    if (sem_can_read_data && sem_can_read_data != SEM_FAILED) {
        sem_close(sem_can_read_data);
    }
    if (sem_can_write_err && sem_can_write_err != SEM_FAILED) {
        sem_close(sem_can_write_err);
    }
    if (sem_can_read_err && sem_can_read_err != SEM_FAILED) {
        sem_close(sem_can_read_err);
    }

    // Выходим
    exit(exit_code);
}

//------------------------------------------------------------------------------
// Примерная логика клиента:
// Ожидается, что аргументы:
//   argv[1] = имя файла (куда писать корректные строки)
//   argv[2] = имя shm для data
//   argv[3] = имя shm для err
//------------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    if (argc < 4) {
        write_str_to_fd(STDERR_FILENO, "Usage: client <filename> <shm_data> <shm_err>\n");
        return 1;
    }

    char *filename    = argv[1]; // Выходной файл
    char *shm_data_nm = argv[2]; // Имя shm (data)
    char *shm_err_nm  = argv[3]; // Имя shm (err)

    // 1) Открываем файл на запись (перезапись + добавление)
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0600);
    if (fd == -1) {
        simple_perror("open output file failed");
        return 1;
    }

    // 2) Подключаемся к разделяемой памяти (DATA)
    int shm_fd_data = shm_open(shm_data_nm, O_RDWR, 0666);
    if (shm_fd_data == -1) {
        simple_perror("shm_open data failed");
        close(fd);
        return 1;
    }
    char *shm_data = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_data, 0);
    if (shm_data == MAP_FAILED) {
        simple_perror("mmap data failed");
        close(fd);
        close(shm_fd_data);
        return 1;
    }
    close(shm_fd_data);  // дескриптор можно закрыть после mmap

    // 3) Подключаемся к разделяемой памяти (ERR)
    int shm_fd_err = shm_open(shm_err_nm, O_RDWR, 0666);
    if (shm_fd_err == -1) {
        simple_perror("shm_open err failed");
        munmap(shm_data, SHM_SIZE);
        close(fd);
        return 1;
    }
    char *shm_err = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_err, 0);
    if (shm_err == MAP_FAILED) {
        simple_perror("mmap err failed");
        munmap(shm_data, SHM_SIZE);
        close(fd);
        close(shm_fd_err);
        return 1;
    }
    close(shm_fd_err);

    // 4) Открываем семафоры (уже созданные родителем)
    sem_t *sem_can_write_data = sem_open(SEM_CAN_WRITE_DATA, 0);
    if (sem_can_write_data == SEM_FAILED) {
        simple_perror("sem_open(SEM_CAN_WRITE_DATA) failed");
        cleanup_and_exit(fd, shm_data, shm_err, NULL, NULL, NULL, NULL, 1);
    }
    sem_t *sem_can_read_data  = sem_open(SEM_CAN_READ_DATA, 0);
    if (sem_can_read_data == SEM_FAILED) {
        simple_perror("sem_open(SEM_CAN_READ_DATA) failed");
        cleanup_and_exit(fd, shm_data, shm_err, sem_can_write_data, NULL, NULL, NULL, 1);
    }
    sem_t *sem_can_write_err = sem_open(SEM_CAN_WRITE_ERR, 0);
    if (sem_can_write_err == SEM_FAILED) {
        simple_perror("sem_open(SEM_CAN_WRITE_ERR) failed");
        cleanup_and_exit(fd, shm_data, shm_err, sem_can_write_data, sem_can_read_data, NULL, NULL, 1);
    }
    sem_t *sem_can_read_err  = sem_open(SEM_CAN_READ_ERR, 0);
    if (sem_can_read_err == SEM_FAILED) {
        simple_perror("sem_open(SEM_CAN_READ_ERR) failed");
        cleanup_and_exit(fd, shm_data, shm_err, sem_can_write_data, sem_can_read_data, sem_can_write_err, NULL, 1);
    }

    // Сообщаем, что клиент запустился
    write_str_to_fd(STDOUT_FILENO, "[Child] started, reading from shm_data...\n");

    while (1) {
        // Ждём разрешения на чтение строки
        if (sem_wait(sem_can_read_data) < 0) {
            // Ошибка или сигнал
            simple_perror("sem_wait(sem_can_read_data) failed");
            break;
        }

        // Проверяем, не пустая ли строка => признак окончания
        if (shm_data[0] == '\0') {
            // Родитель сообщил об окончании ввода
            break;
        }

        // Копируем строку локально
        char buf[4096];
        memset(buf, 0, sizeof(buf));
        strncpy(buf, shm_data, sizeof(buf)-1);

        // Проверяем, заканчивается ли '.' или ';'
        size_t len = strlen(buf);
        if (len > 0) {
            char last_char = buf[len - 1];
            if (last_char != '.' && last_char != ';') {
                // Ошибка. Пишем в shm_err
                if (sem_wait(sem_can_write_err) == 0) {
                    // Собираем сообщение в shm_err
                    shm_err[0] = '\0';
                    strcat(shm_err, "child error: string does not end with '.' or ';'. The string was: ");
                    strcat(shm_err, buf);
                    sem_post(sem_can_read_err);
                } else {
                    // sem_wait не сработал
                    simple_perror("sem_wait(sem_can_write_err) failed");
                }
            } else {
                // Строка корректна, пишем в файл + \n
                strcat(buf, "\n");
                ssize_t written = write(fd, buf, strlen(buf));
                if (written < 0) {
                    // Ошибка записи
                    if (sem_wait(sem_can_write_err) == 0) {
                        shm_err[0] = '\0';
                        strcat(shm_err, "child error: write to file failed, errno=");
                        // Если нужно вывести число errno, нужно int->str. Упростим:
                        // Выведем просто "... , errno\n"
                        strcat(shm_err, "?\n");
                        sem_post(sem_can_read_err);
                    }
                }
            }
        }

        // Освобождаем буфер data
        sem_post(sem_can_write_data);
    }

    // Сообщаем, что завершаемся
    write_str_to_fd(STDOUT_FILENO, "[Child] finishing.\n");

    cleanup_and_exit(fd, shm_data, shm_err,
                     sem_can_write_data, sem_can_read_data,
                     sem_can_write_err, sem_can_read_err,
                     0);
    return 0;
}
