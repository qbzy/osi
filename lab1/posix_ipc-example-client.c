#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>



int main(int argc, char **argv) {
    char buf[4096];
    ssize_t bytes;


    // NOTE: `O_WRONLY` only enables file for writing
    // NOTE: `O_CREAT` creates the requested file if absent
    // NOTE: `O_TRUNC` empties the file prior to opening
    // NOTE: `O_APPEND` subsequent writes are being appended instead of overwritten
    int32_t file = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0600);
    if (file == -1) {
        const char msg[] = "error: failed to open requested file\n";
        write(STDERR_FILENO, msg, sizeof(msg));
        exit(EXIT_FAILURE);
    }


    while ((bytes = read(STDIN_FILENO, buf, sizeof(buf)))) {
        int8_t f = 1;
        if (bytes < 0) {
            const char msg[] = "error: failed to read from stdin\n";
            write(STDERR_FILENO, msg, sizeof(msg));
            exit(EXIT_FAILURE);
        } else if (buf[0] == '\n') {
            // NOTE: When Enter is pressed with no input, then exit client
            break;
        }


        {
            if (buf[bytes - 2] != ';' && buf[bytes - 2] != '.'){

                char msg[4096];
                int len = snprintf(msg, sizeof(msg), "child error: string does not end with ; or . Error string: %s", buf);

                write(STDERR_FILENO, msg, len);
                f = 0;

            }
        }

        {
            // NOTE: Replace newline with NULL-terminator
            buf[bytes - 1] = '\n';
            if (f != 0) {
                int32_t written = write(file, buf, bytes);
                if (written != bytes) {
                    const char msg[] = "error: failed to write to file\n";
                    write(STDERR_FILENO, msg, sizeof(msg));
                    exit(EXIT_FAILURE);
                }
            }
            memset(buf, 0,sizeof(buf));
        }

    }


    close(file);
}