#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <string.h>

typedef struct CustomAllocator CustomAllocator;

typedef CustomAllocator *(*create_t)(void *, size_t);

typedef void (*destroy_t)(CustomAllocator *);

typedef void *(*alloc_t)(CustomAllocator *, size_t);

typedef void (*free_t)(CustomAllocator *, void *);


void *fallback_alloc(CustomAllocator *dummy, size_t sz) {
    (void) dummy;
    return mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

void fallback_free(CustomAllocator *dummy, void *ptr) {
    (void) dummy;
    munmap(ptr, malloc_usable_size(ptr));
}

// Упрощённая функция вывода
static void print_msg(const char *txt) {
    write(STDOUT_FILENO, txt, strlen(txt));
}

void alloc_test(CustomAllocator *alloc, alloc_t allocate, free_t release) {
    void *chunks[50];
    for (int i = 0; i < 50; i++) {
        chunks[i] = allocate(alloc, 2048);
        if (!chunks[i]) {
            print_msg("ALLOC TEST: allocation failed at index ");
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "%d\n", i);
            print_msg(tmp);
            return;
        }
    }
    // Освобождаем
    for (int i = 0; i < 50; i++) {
        release(alloc, chunks[i]);
    }
    print_msg("ALLOC TEST: success\n");
}

void merge_test(CustomAllocator *alloc, alloc_t allocate, free_t release) {
    void *parts[8];
    for (int i = 0; i < 8; i++) {
        parts[i] = allocate(alloc, 1024);
        if (!parts[i]) {
            print_msg("MERGE TEST: allocation failed at index ");
            char buf[16];
            snprintf(buf, sizeof(buf), "%d\n", i);
            print_msg(buf);
            return;
        }
    }
    // Освобождаем в разном порядке
    release(alloc, parts[2]);
    release(alloc, parts[5]);
    release(alloc, parts[0]);
    release(alloc, parts[6]);

    // Пробуем взять большой блок
    void *large = allocate(alloc, 4096);
    if (large) {
        print_msg("MERGE TEST: passed\n");
        release(alloc, large);
    } else {
        print_msg("MERGE TEST: failed\n");
    }
    // Освободим оставшиеся
    for (int i = 1; i < 8; i++) {
        if (parts[i] != NULL) {
            release(alloc, parts[i]);
        }
    }
}

void speed_test(CustomAllocator *alloc, alloc_t allocate, free_t release) {
    clock_t t1 = clock();
    for (int i = 0; i < 2000; i++) {
        void *mem = allocate(alloc, 256);
        if (mem) {
            release(alloc, mem);
        }
    }
    clock_t t2 = clock();
    double total_time = (double) (t2 - t1) / CLOCKS_PER_SEC;

    char msg[64];
    snprintf(msg, sizeof(msg), "SPEED TEST: done in %.6f s\n", total_time);
    print_msg(msg);
}

void fragmentation_test(CustomAllocator *alloc, alloc_t allocate, free_t release) {
    void *segments[12];
    for (int i = 0; i < 12; i++) {
        segments[i] = allocate(alloc, 512);
        if (!segments[i]) {
            print_msg("FRAGMENTATION TEST: allocation failed\n");
            return;
        }
    }
    // Освобождаем выборочно
    release(alloc, segments[3]);
    release(alloc, segments[7]);
    release(alloc, segments[10]);

    // Проверим выделение более крупного куска
    void *big = allocate(alloc, 1536);
    if (big) {
        print_msg("FRAGMENTATION TEST: passed\n");
        release(alloc, big);
    } else {
        print_msg("FRAGMENTATION TEST: failed\n");
    }
    // Освободим остальные
    for (int i = 0; i < 12; i++) {
        if (segments[i] != NULL) {
            release(alloc, segments[i]);
        }
    }
}

int main(int argc, char **argv) {
    void *lib_handle = NULL;
    create_t fn_create = NULL;
    destroy_t fn_destroy = NULL;
    alloc_t fn_alloc = NULL;
    free_t fn_free = NULL;

    // Если в аргументах указан .so/.dll — пробуем открыть
    if (argc > 1) {
        lib_handle = dlopen(argv[1], RTLD_NOW);
        if (lib_handle) {
            fn_create = (create_t) dlsym(lib_handle, "allocator_create");
            fn_destroy = (destroy_t) dlsym(lib_handle, "allocator_destroy");
            fn_alloc = (alloc_t) dlsym(lib_handle, "allocator_alloc");
            fn_free = (free_t) dlsym(lib_handle, "allocator_free");
        }
    }

    // Если не удалось — используем fallback-аллокатор
    if (!lib_handle || !fn_create || !fn_destroy || !fn_alloc || !fn_free) {
        print_msg("No custom library. Using fallback allocator.\n");
        fn_create = (create_t) malloc;
        fn_destroy = free;
        fn_alloc = fallback_alloc;
        fn_free = fallback_free;
    }

    // Выделим область под наш «CustomAllocator» (1 МБ)
    void *region = mmap(NULL, 1024 * 1024,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS,
                        -1, 0);

    CustomAllocator *my_allocator = fn_create(region, 1024 * 1024);

    // Запускаем тесты
    alloc_test(my_allocator, fn_alloc, fn_free);
    merge_test(my_allocator, fn_alloc, fn_free);
    speed_test(my_allocator, fn_alloc, fn_free);
    fragmentation_test(my_allocator, fn_alloc, fn_free);

    // Завершаем работу
    fn_destroy(my_allocator);
    munmap(region, 1024 * 1024);

    if (lib_handle) {
        dlclose(lib_handle);
    }

    return 0;
}
