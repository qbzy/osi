#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

// Было: typedef struct Node { size_t capacity; struct Node* next; } Node;
// Становится: typedef struct Chunk { size_t length; struct Chunk* next; } Chunk;
typedef struct Chunk {
    size_t length;
    struct Chunk* next;
} Chunk;

typedef struct {
    Chunk* free_list;
} AllocFF;

// Внешний интерфейс
AllocFF* allocator_create(void* mem, size_t sz) {
    AllocFF* a = (AllocFF*)mem;
    a->free_list = (Chunk*)((char*)mem + sizeof(AllocFF));
    a->free_list->length = sz - sizeof(AllocFF) - sizeof(Chunk);
    a->free_list->next = NULL;
    return a;
}

void allocator_destroy(AllocFF* a) {
    (void)a; // Ничего не делаем
}

void* allocator_alloc(AllocFF* a, size_t needed) {
    Chunk* prev = NULL;
    Chunk* cur = a->free_list;

    while (cur) {
        if (cur->length >= needed) {
            // Можно разместить
            if (cur->length - needed > sizeof(Chunk)) {
                // Разделяем блок
                char* split_addr = (char*)cur + sizeof(Chunk) + needed;
                Chunk* new_chunk = (Chunk*)split_addr;
                new_chunk->length = cur->length - needed - sizeof(Chunk);
                new_chunk->next = cur->next;

                // Текущему уменьшаем length
                cur->length = needed;

                // Вставляем new_chunk в список
                if (prev) {
                    prev->next = new_chunk;
                } else {
                    a->free_list = new_chunk;
                }
            } else {
                // Берём целиком
                if (prev) {
                    prev->next = cur->next;
                } else {
                    a->free_list = cur->next;
                }
            }
            return (char*)cur + sizeof(Chunk);
        }
        // Двигаемся дальше
        prev = cur;
        cur  = cur->next;
    }
    // Не нашли подходящий блок
    return NULL;
}

void allocator_free(AllocFF* a, void* mem) {
    if (!mem) return;
    Chunk* freed = (Chunk*)((char*)mem - sizeof(Chunk));

    // Кладём освобождённый блок в начало списка
    freed->next = a->free_list;
    a->free_list = freed;
}
