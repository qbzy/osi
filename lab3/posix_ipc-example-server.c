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
#include <limits.h>

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

static char CLIENT_PROGRAM_NAME[] = "client";

//------------------------------------------------------------------------------
// Простейшая функция для вывода C-строки (null-terminated) в указанный дескриптор.
static void write_str_to_fd(int fd, const char *s)
{
    if (s) {
        write(fd, s, strlen(s));
    }
}

//------------------------------------------------------------------------------
// Считываем *одну строку* с терминала (STDIN_FILENO) без использования stdio.h.
// Возвращаем количество реально прочитанных байт (не включая '\0' в конце).
// Если прочитан '\n', заменяем его на '\0'. Если EOF или ошибка — возвращаем 0.
// Буфер должен быть размером не меньше (buf_size).
//------------------------------------------------------------------------------
static ssize_t read_line_from_stdin(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) {
        return 0;
    }

    size_t pos = 0;
    char ch;
    while (1) {
        ssize_t rd = read(STDIN_FILENO, &ch, 1);
        if (rd < 1) {
            // EOF или ошибка
            break;
        }

        // Если встретили '\n' — завершаем строку
        if (ch == '\n') {
            break;
        }

        if (pos < buf_size - 1) {
            buf[pos++] = ch;
        }
        else {
            // Достигли конца буфера — больше не пишем
            // но продолжаем считывать, чтобы очистить stdin до конца строки
        }
    }

    // Добавим null-terminator
    buf[pos] = '\0';
    return (ssize_t)pos;
}

//------------------------------------------------------------------------------
// Простейшая функция вместо perror — выводит указанное сообщение + '\n'
// Можно дополнительно вывести значение errno, если нужно.
//------------------------------------------------------------------------------
static void simple_perror(const char *msg)
{
    // Выводим сам msg
    write_str_to_fd(STDERR_FILENO, msg);

    // Можно добавить что-то вроде ": error\n"
    // Или, если хотим вывести errno в виде числа, нужен int->string
    write_str_to_fd(STDERR_FILENO, "\n");
}

//------------------------------------------------------------------------------

int main(void)
{
    // 1) Считываем имя файла
    char file[4096];
    write_str_to_fd(STDOUT_FILENO, "Enter filename: ");
    ssize_t flen = read_line_from_stdin(file, sizeof(file));
    if (flen <= 0) {
        write_str_to_fd(STDERR_FILENO, "Enter filename failed or EOF\n");
        exit(EXIT_FAILURE);
    }

    // 2) Создаем/открываем shm (DATA)
    int shm_fd_data = shm_open(SHM_DATA_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd_data == -1) {
        simple_perror("shm_open data");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd_data, SHM_SIZE) == -1) {
        simple_perror("ftruncate data");
        shm_unlink(SHM_DATA_NAME);
        exit(EXIT_FAILURE);
    }
    char *shm_data = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_data, 0);
    if (shm_data == MAP_FAILED) {
        simple_perror("mmap data");
        shm_unlink(SHM_DATA_NAME);
        exit(EXIT_FAILURE);
    }
    memset(shm_data, 0, SHM_SIZE);

    // 3) Создаем/открываем shm (ERRORS)
    int shm_fd_err = shm_open(SHM_ERR_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd_err == -1) {
        simple_perror("shm_open err");
        munmap(shm_data, SHM_SIZE);
        shm_unlink(SHM_DATA_NAME);
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd_err, SHM_SIZE) == -1) {
        simple_perror("ftruncate err");
        shm_unlink(SHM_ERR_NAME);
        munmap(shm_data, SHM_SIZE);
        shm_unlink(SHM_DATA_NAME);
        exit(EXIT_FAILURE);
    }
    char *shm_err = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_err, 0);
    if (shm_err == MAP_FAILED) {
        simple_perror("mmap err");
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
        simple_perror("sem_open write_data");
        munmap(shm_data, SHM_SIZE);
        munmap(shm_err, SHM_SIZE);
        shm_unlink(SHM_DATA_NAME);
        shm_unlink(SHM_ERR_NAME);
        exit(EXIT_FAILURE);
    }

    sem_unlink(SEM_CAN_READ_DATA);
    sem_t* sem_can_read_data = sem_open(SEM_CAN_READ_DATA, O_CREAT, 0666, 0);
    if (sem_can_read_data == SEM_FAILED) {
        simple_perror("sem_open read_data");
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
        simple_perror("sem_open write_err");
        // по аналогии освобождаем всё
        // ...
        exit(EXIT_FAILURE);
    }

    sem_unlink(SEM_CAN_READ_ERR);
    sem_t* sem_can_read_err = sem_open(SEM_CAN_READ_ERR, O_CREAT, 0666, 0);
    if (sem_can_read_err == SEM_FAILED) {
        simple_perror("sem_open read_err");
        // по аналогии освобождаем всё
        // ...
        exit(EXIT_FAILURE);
    }

    // 6) Получаем путь к каталогу текущего исполняемого файла (readlink)
    //    Ищем последний '/', чтобы обрезать до директории
    char progpath[1024];
    ssize_t len = readlink("/proc/self/exe", progpath, sizeof(progpath)-1);
    if (len == -1) {
        simple_perror("readlink");
        // чистим всё
        exit(EXIT_FAILURE);
    }
    while (len > 0 && progpath[len] != '/')
        --len;
    progpath[len] = '\0';

    // 7) Запускаем дочерний процесс (fork)
    pid_t child = fork();
    if (child < 0) {
        simple_perror("fork");
        // чистим
        exit(EXIT_FAILURE);
    }
    else if (child == 0) {
        // child
        char path[1024];
        // Собираем что-то вроде /home/user/.../client
        // без snprintf (он из stdio.h), используем strncat и т.д.
        // Но можно сделать так:
        memset(path, 0, sizeof(path));
        strncpy(path, progpath, sizeof(path) - 1);
        strncat(path, "/", sizeof(path) - strlen(path) - 1);
        strncat(path, CLIENT_PROGRAM_NAME, sizeof(path) - strlen(path) - 1);

        // Формируем аргументы для execv
        char *const args[] = {
                CLIENT_PROGRAM_NAME,
                file,
                SHM_DATA_NAME,
                SHM_ERR_NAME,
                NULL
        };
        execv(path, args);

        // если execv вернулся — ошибка
        write_str_to_fd(STDERR_FILENO, "execv failed\n");
        _exit(EXIT_FAILURE);
    }
    else {
        // parent
        pid_t pid = getpid();

        // Аналог "printf("%d: I'm a parent...")":
        char msg[256];
        memset(msg, 0, sizeof(msg));

        // Для вывода pid и child можно написать небольшую int->str, но для краткости выведем без них:
        // Или вывести "Parent started\n"
        write_str_to_fd(STDOUT_FILENO, "Parent started. Start typing lines. Press Enter on empty line or Ctrl-D to exit\n");

        // Основной цикл ввода от пользователя
        char input_buf[4096];
        while (1) {
            // 1) Неблокирующе проверим, нет ли ошибок от ребёнка
            if (sem_trywait(sem_can_read_err) == 0) {
                // читаем ошибку из shm_err
                // затем освобождаем буфер для следующей ошибки
                write_str_to_fd(STDOUT_FILENO, "[Parent] Child error message: ");
                write_str_to_fd(STDOUT_FILENO, shm_err);
                write_str_to_fd(STDOUT_FILENO, "\n");
                sem_post(sem_can_write_err);
            }
            // если -1 c EAGAIN — нет ошибок, идём дальше

            // 2) Читаем строку
            write_str_to_fd(STDOUT_FILENO, "> "); // чтобы было видно приглашение
            ssize_t rdlen = read_line_from_stdin(input_buf, sizeof(input_buf));
            if (rdlen <= 0) {
                // EOF или ошибка
                break;
            }
            // Если пользователь ввёл пустую строку — завершаем
            if (input_buf[0] == '\0') {
                break;
            }

            // 3) Пишем в shm_data
            sem_wait(sem_can_write_data); // ожидаем, что буфер свободен
            strncpy(shm_data, input_buf, SHM_SIZE - 1);
            shm_data[SHM_SIZE - 1] = '\0';
            sem_post(sem_can_read_data);
        }

        // Сигнализируем ребёнку, что данных больше не будет (пустая строка)
        sem_wait(sem_can_write_data);
        shm_data[0] = '\0';
        sem_post(sem_can_read_data);

        // Ждём, пока ребёнок завершится
        int status = 0;
        waitpid(child, &status, 0);

        // Закрываем семафоры
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

        write_str_to_fd(STDOUT_FILENO, "Parent finished.\n");
    }

    return 0;
}
