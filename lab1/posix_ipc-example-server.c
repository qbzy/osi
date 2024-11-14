#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char CLIENT_PROGRAM_NAME[] = "posix_ipc-example-client";


int main() {

    char file[4096];
    write(STDIN_FILENO, "Enter filename: ", 16);
    if (scanf("%s", file) <= 0){

        char mssg[1024];
        uint32_t len = snprintf(mssg, sizeof(mssg) - 1, "Enter filename failed\n");
        write(STDERR_FILENO, mssg, len);
        exit(EXIT_SUCCESS);
    }



    // NOTE: Get full path to the directory, where program resides
    char progpath[1024];
    {
        // NOTE: Read full program path, including its name
        ssize_t len = readlink("/proc/self/exe", progpath, sizeof(progpath) - 1);
        if (len == -1) {
            const char msg[] = "error: failed to read full program path\n";
            write(STDERR_FILENO, msg, sizeof(msg));
            exit(EXIT_FAILURE);
        }

        // NOTE: Trim the path to first slash from the end
        while (progpath[len] != '/')
            --len;

        progpath[len] = '\0';
    }

    // NOTE: Open pipe
    int channel_data[2];
    int channel_errors[2];
    if (pipe(channel_data) == -1) {
        const char msg[] = "error: failed to create pipe\n";
        write(STDERR_FILENO, msg, sizeof(msg));
        exit(EXIT_FAILURE);
    }
    if (pipe(channel_errors) == -1) {
        const char msg[] = "error: failed to create pipe\n";
        write(STDERR_FILENO, msg, sizeof(msg));
        exit(EXIT_FAILURE);
    }


    // NOTE: Spawn a new process
    const pid_t child = fork();


    switch (child) {
        case -1: { // NOTE: Kernel fails to create another process
            const char msg[] = "error: failed to spawn new process\n";
            write(STDERR_FILENO, msg, sizeof(msg));
            exit(EXIT_FAILURE);
        } break;

        case 0: { // NOTE: We're a child, child doesn't know its pid after fork
            pid_t pid = getpid(); // NOTE: Get child PID
            close(channel_data[1]);  // Unused in child
            close(channel_errors[0]); // Unused in child
            // NOTE: Connect parent stdin to child stdin
            dup2(channel_data[0], STDIN_FILENO);    // Перенаправляем stdin на чтение из channel_data[0]
            dup2(channel_errors[1], STDERR_FILENO);




            {
                char msg[64];
                const int32_t length = snprintf(msg, sizeof(msg),
                                                "%d: I'm a child\n", pid);
                write(STDOUT_FILENO, msg, length);
            }

            {
                char path[1024];
                snprintf(path, sizeof(path) - 1, "%s/%s", progpath, CLIENT_PROGRAM_NAME);
                // NOTE: args[0] must be a program name, next the actual arguments
                // NOTE: `NULL` at the end is mandatory, because `exec*`
                //       expects a NULL-terminated list of C-strings
                char *const args[] = {CLIENT_PROGRAM_NAME, file, NULL};

                int32_t status = execv(path, args);

                if (status == -1) {
                    const char msg[] = "error: failed to exec into new executable image\n";
                    write(STDERR_FILENO, msg, sizeof(msg));
                    exit(EXIT_FAILURE);
                }
            }

        } break;

        default: { // NOTE: We're a parent, parent knows PID of child after fork
            pid_t pid = getpid(); // NOTE: Get parent PID
            char buf1[4096];
            char buf[4096];
            ssize_t bytes;
            ssize_t bytes1;

            close(channel_data[0]);
            close(channel_errors[1]);

            {
                char msg[64];
                const int32_t length = snprintf(msg, sizeof(msg),
                                                "%d: I'm a parent, my child has PID %d\n", pid, child);
                write(STDOUT_FILENO, msg, length);
            }
            {
                char msg[128];
                int32_t len = snprintf(msg, sizeof(msg) - 1,
                                       "%d: Start typing lines of text. Press 'Ctrl-D' or 'Enter' with no input to exit\n", pid);
                write(STDOUT_FILENO, msg, len);
            }



            fcntl(channel_errors[0], F_SETFL, O_NONBLOCK); // Устанавливаем неблокирующий режим для channel_errors[0]

            while ((bytes1 = read(STDIN_FILENO, buf1, sizeof(buf1))) ) {
                // Читаем ошибки, если они есть
                ssize_t error_bytes = read(channel_errors[0], buf, sizeof(buf));
                if (error_bytes > 0) {
                    char msg[64];
                    int32_t length = snprintf(msg, sizeof(msg), "%d: I'm a parent, my child has PID %d\n", pid, child);
                    write(STDOUT_FILENO, msg, length);

                    write(STDOUT_FILENO, buf, error_bytes); // Выводим только реальное количество считанных байт
                }


                if (bytes1 < 0){
                    const char msg[] = "error: failed to read from stdin\n";
                    write(STDERR_FILENO, msg, sizeof(msg));
                    exit(EXIT_FAILURE);
                }
                if (buf1[0] == '\n') {
                    break; // Выход, если введена пустая строка
                }

                // Отправляем строку клиенту
                if (write(channel_data[1], buf1, bytes1) == -1) {
                    const char msg[] = "error: failed to write to channel_data\n";
                    write(STDERR_FILENO, msg, sizeof(msg));
                    exit(EXIT_FAILURE);
                }


            }

            close(channel_data[1]);
            close(channel_errors[0]);



            // NOTE: `wait` blocks the parent until child exits
            int child_status;
            wait(&child_status);
            if (WIFEXITED(child_status) && WEXITSTATUS(child_status) != EXIT_SUCCESS) {
                const char msg[] = "error: child exited with error\n";
                write(STDERR_FILENO, msg, sizeof(msg) - 1);
                exit(WEXITSTATUS(child_status));
            }

        } break;
    }
}