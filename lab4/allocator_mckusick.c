#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define ALIGN_SIZE(size, alignment) (((size) + (alignment - 1)) & ~(alignment - 1))
#define FREE_LIST_ALIGNMENT 8
#define NUM_FREE_LISTS 32 // Количество списков для блоков разного размера

typedef struct Block {
    size_t size;
    struct Block* next;
} Block;

typedef struct Allocator {
    void* memory;
    size_t size;
    Block* free_lists[NUM_FREE_LISTS]; // Массив списков свободных блоков
} Allocator;

// Функция для определения индекса в массиве free_lists
size_t get_free_list_index(size_t size) {
    size_t index = 0;
    while (size > (1 << (index + FREE_LIST_ALIGNMENT))) {
        index++;
    }
    return index < NUM_FREE_LISTS ? index : NUM_FREE_LISTS - 1;
}

Allocator* allocator_create(void* memory, size_t size) {
    if (memory == NULL || size < sizeof(Allocator)) {
        return NULL;
    }

    Allocator* allocator = (Allocator*)memory;
    allocator->memory = (char*)memory + sizeof(Allocator);
    allocator->size = size - sizeof(Allocator);

    // Инициализация всех списков свободных блоков
    for (size_t i = 0; i < NUM_FREE_LISTS; i++) {
        allocator->free_lists[i] = NULL;
    }

    // Добавляем всю доступную память в список для максимального размера
    size_t index = get_free_list_index(allocator->size);
    Block* initial_block = (Block*)allocator->memory;
    initial_block->size = allocator->size;
    initial_block->next = allocator->free_lists[index];
    allocator->free_lists[index] = initial_block;

    return allocator;
}

void allocator_destroy(Allocator* allocator) {
    if (allocator == NULL) {
        return;
    }

    allocator->memory = NULL;
    allocator->size = 0;
    for (size_t i = 0; i < NUM_FREE_LISTS; i++) {
        allocator->free_lists[i] = NULL;
    }
}

void* allocator_alloc(Allocator* allocator, size_t size) {
    if (allocator == NULL || size == 0) {
        return NULL;
    }

    size_t aligned_size = ALIGN_SIZE(size, FREE_LIST_ALIGNMENT);
    size_t index = get_free_list_index(aligned_size);

    // Ищем подходящий блок в списке
    while (index < NUM_FREE_LISTS) {
        Block* prev = NULL;
        Block* curr = allocator->free_lists[index];

        while (curr != NULL) {
            if (curr->size >= aligned_size) {
                // Нашли подходящий блок
                if (prev != NULL) {
                    prev->next = curr->next;
                } else {
                    allocator->free_lists[index] = curr->next;
                }

                // Если остаток блока достаточно большой, разделяем его
                if (curr->size > aligned_size + sizeof(Block)) {
                    Block* new_block = (Block*)((char*)curr + sizeof(Block) + aligned_size);
                    new_block->size = curr->size - sizeof(Block) - aligned_size;
                    size_t new_index = get_free_list_index(new_block->size);
                    new_block->next = allocator->free_lists[new_index];
                    allocator->free_lists[new_index] = new_block;

                    curr->size = aligned_size;
                }

                return (void*)((char*)curr + sizeof(Block));
            }

            prev = curr;
            curr = curr->next;
        }

        // Переходим к следующему списку с блоками большего размера
        index++;
    }

    return NULL; // Не удалось найти подходящий блок
}

void allocator_free(Allocator* allocator, void* memory) {
    if (allocator == NULL || memory == NULL) {
        return;
    }

    Block* block = (Block*)((char*)memory - sizeof(Block));
    size_t index = get_free_list_index(block->size);

    // Добавляем освобождённый блок в соответствующий список
    block->next = allocator->free_lists[index];
    allocator->free_lists[index] = block;

    // Слияние соседних свободных блоков
    for (size_t i = 0; i < NUM_FREE_LISTS; i++) {
        Block* curr = allocator->free_lists[i];
        while (curr != NULL && curr->next != NULL) {
            if ((char*)curr + sizeof(Block) + curr->size == (char*)curr->next) {
                // Сливаем два соседних блока
                curr->size += sizeof(Block) + curr->next->size;
                curr->next = curr->next->next;

                // Перемещаем объединённый блок в соответствующий список
                size_t new_index = get_free_list_index(curr->size);
                if (new_index != i) {
                    Block* merged_block = curr;
                    curr = curr->next; // Продолжаем обход
                    merged_block->next = allocator->free_lists[new_index];
                    allocator->free_lists[new_index] = merged_block;
                }
            } else {
                curr = curr->next;
            }
        }
    }
}