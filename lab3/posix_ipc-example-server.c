#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <semaphore.h>
#include <errno.h>

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
static char CLIENT_PROGRAM_NAME[] = "posix_ipc-example-client";

int main(void)
{
    // 1) Считываем имя файла
    char file[4096];
    printf("Enter filename: ");
    if (scanf("%4095s", file) <= 0) {
        fprintf(stderr, "Enter filename failed\n");
        exit(EXIT_FAILURE);
    }
    // опустошаем stdin
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }

    // 2) Создаем/открываем shm (DATA)
    int shm_fd_data = shm_open(SHM_DATA_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd_data == -1) {
        perror("shm_open data");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd_data, SHM_SIZE) == -1) {
        perror("ftruncate data");
        shm_unlink(SHM_DATA_NAME);
        exit(EXIT_FAILURE);
    }
    char *shm_data = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_data, 0);
    if (shm_data == MAP_FAILED) {
        perror("mmap data");
        shm_unlink(SHM_DATA_NAME);
        exit(EXIT_FAILURE);
    }
    memset(shm_data, 0, SHM_SIZE);

    // 3) Создаем/открываем shm (ERRORS)
    int shm_fd_err = shm_open(SHM_ERR_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd_err == -1) {
        perror("shm_open err");
        // чистим ресурсы
        munmap(shm_data, SHM_SIZE);
        shm_unlink(SHM_DATA_NAME);
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd_err, SHM_SIZE) == -1) {
        perror("ftruncate err");
        shm_unlink(SHM_ERR_NAME);
        // чистим ресурсы
        munmap(shm_data, SHM_SIZE);
        shm_unlink(SHM_DATA_NAME);
        exit(EXIT_FAILURE);
    }
    char *shm_err = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_err, 0);
    if (shm_err == MAP_FAILED) {
        perror("mmap err");
        // чистим ресурсы
        munmap(shm_data, SHM_SIZE);
        shm_unlink(SHM_DATA_NAME);
        shm_unlink(SHM_ERR_NAME);
        exit(EXIT_FAILURE);
    }
    memset(shm_err, 0, SHM_SIZE);

    // 4) Создаем семафоры для data
    sem_unlink(SEM_CAN_WRITE_DATA);
    sem_t* sem_can_write_data = sem_open(SEM_CAN_WRITE_DATA, O_CREAT, 0666, 1);
    if (sem_can_write_data == SEM_FAILED) {
        perror("sem_open write_data");
        // чистим
        munmap(shm_data, SHM_SIZE);
        munmap(shm_err, SHM_SIZE);
        shm_unlink(SHM_DATA_NAME);
        shm_unlink(SHM_ERR_NAME);
        exit(EXIT_FAILURE);
    }

    sem_unlink(SEM_CAN_READ_DATA);
    sem_t* sem_can_read_data = sem_open(SEM_CAN_READ_DATA, O_CREAT, 0666, 0);
    if (sem_can_read_data == SEM_FAILED) {
        perror("sem_open read_data");
        // чистим
        sem_close(sem_can_write_data);
        sem_unlink(SEM_CAN_WRITE_DATA);
        munmap(shm_data, SHM_SIZE);
        munmap(shm_err, SHM_SIZE);
        shm_unlink(SHM_DATA_NAME);
        shm_unlink(SHM_ERR_NAME);
        exit(EXIT_FAILURE);
    }

    // 5) Создаем семафоры для err
    sem_unlink(SEM_CAN_WRITE_ERR);
    sem_t* sem_can_write_err = sem_open(SEM_CAN_WRITE_ERR, O_CREAT, 0666, 1);
    if (sem_can_write_err == SEM_FAILED) {
        perror("sem_open write_err");
        // чистим
        // ...
        // Для краткости не пишу весь повторяющийся код освобождения
        // по аналогии, надо закрыть/unlink-нуть предыдущие семафоры и shm
        exit(EXIT_FAILURE);
    }

    sem_unlink(SEM_CAN_READ_ERR);
    sem_t* sem_can_read_err = sem_open(SEM_CAN_READ_ERR, O_CREAT, 0666, 0);
    if (sem_can_read_err == SEM_FAILED) {
        perror("sem_open read_err");
        // чистим всё, как выше
        exit(EXIT_FAILURE);
    }

    // 6) Получаем путь к каталогу текущего исполняемого файла
    char progpath[1024];
    ssize_t len = readlink("/proc/self/exe", progpath, sizeof(progpath)-1);
    if (len == -1) {
        perror("readlink");
        // чистим всё
        exit(EXIT_FAILURE);
    }
    while (len > 0 && progpath[len] != '/')
        --len;
    progpath[len] = '\0';

    // 7) Запускаем дочерний процесс
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        // чистим
        exit(EXIT_FAILURE);
    } else if (child == 0) {
        // child
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", progpath, CLIENT_PROGRAM_NAME);

        // Передаем 5 аргументов:
        // 1) имя программы
        // 2) имя файла
        // 3) shm_data
        // 4) shm_err
        // (больше ничего особо не нужно, семафоры имеют зафиксированные имена)
        char *const args[] = {
                CLIENT_PROGRAM_NAME,
                file,
                SHM_DATA_NAME,
                SHM_ERR_NAME,
                NULL
        };
        execv(path, args);
        perror("execv");
        _exit(EXIT_FAILURE);
    } else {
        // parent
        pid_t pid = getpid();
        printf("%d: I'm a parent, my child has PID %d\n", pid, child);
        printf("%d: Start typing lines. Press Enter on empty line or Ctrl-D to exit\n", pid);

        // Основной цикл ввода от пользователя
        char input_buf[4096];
        while (1) {
            // 1) Периодически проверим, нет ли ошибок от ребёнка (неблокирующе)
            {
                // пробуем sem_trywait, если успех — значит есть ошибка
                if (sem_trywait(sem_can_read_err) == 0) {
                    // читаем ошибку из shm_err
                    // при этом надо быть уверенным, что ребёнок уже записал
                    // (ребёнок перед записью подождёт sem_can_write_err)
                    printf("[Parent] Child error message: %s\n", shm_err);

                    // сообщим, что освободили буфер для новых ошибок
                    sem_post(sem_can_write_err);
                }
                // если вернулся -1 c EAGAIN — нет ошибок, продолжаем
            }

            // 2) Считываем строчку от пользователя
            if (!fgets(input_buf, sizeof(input_buf), stdin)) {
                // EOF или ошибка чтения -> завершаем
                break;
            }
            if (input_buf[0] == '\n') {
                // пустая строка — завершаем
                break;
            }

            // Убираем \n, если есть
            size_t ln = strlen(input_buf);
            if (ln > 0 && input_buf[ln-1] == '\n') {
                input_buf[ln-1] = '\0';
            }

            // 3) Пишем в shm_data
            sem_wait(sem_can_write_data); // ожидаем, что буфер свободен
            strncpy(shm_data, input_buf, SHM_SIZE-1);
            shm_data[SHM_SIZE-1] = '\0';
            sem_post(sem_can_read_data);

            // По желанию можно снова посмотреть, нет ли ошибок, и так по кругу
        }

        // Сигнализируем ребёнку, что данных больше не будет
        sem_wait(sem_can_write_data);
        shm_data[0] = '\0'; // пустая строка => ребёнок выйдет
        sem_post(sem_can_read_data);

        // Ждём, пока ребёнок завершится
        int status = 0;
        waitpid(child, &status, 0);

        // Закрываем семафоры, unlink
        sem_close(sem_can_write_data);
        sem_close(sem_can_read_data);
        sem_unlink(SEM_CAN_WRITE_DATA);
        sem_unlink(SEM_CAN_READ_DATA);

        sem_close(sem_can_write_err);
        sem_close(sem_can_read_err);
        sem_unlink(SEM_CAN_WRITE_ERR);
        sem_unlink(SEM_CAN_READ_ERR);

        // Отключаем shm
        munmap(shm_data, SHM_SIZE);
        shm_unlink(SHM_DATA_NAME);

        munmap(shm_err, SHM_SIZE);
        shm_unlink(SHM_ERR_NAME);

        printf("%d: Parent finished\n", pid);
    }

    return 0;
}
