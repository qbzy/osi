#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef struct Block {
    size_t size;
    struct Block* next;
} Block;

typedef struct Allocator{
    void* memory;
    size_t size;
    Block* free_list;
} Allocator;

Allocator* allocator_create(void* memory, size_t size) {
    Allocator* allocator = (Allocator*)mmap(NULL, sizeof(Allocator), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    allocator->memory = memory;
    allocator->size = size;
    allocator->free_list = (Block*)memory;
    allocator->free_list->size = size;
    allocator->free_list->next = NULL;
    return allocator;
}

void allocator_destroy(Allocator* allocator) {
    munmap(allocator, sizeof(Allocator));
}

void* allocator_alloc(Allocator* allocator, size_t size) {
    Block* prev = NULL;
    Block* curr = allocator->free_list;

    while (curr != NULL) {
        if (curr->size >= size) {
            if (curr->size > size + sizeof(Block)) {
                Block* new_block = (Block*)((char*)curr + sizeof(Block) + size);
                new_block->size = curr->size - size - sizeof(Block);
                new_block->next = curr->next;
                curr->size = size;
                curr->next = new_block;
            }
            if (prev == NULL) {
                allocator->free_list = curr->next;
            } else {
                prev->next = curr->next;
            }
            return (void*)((char*)curr + sizeof(Block));
        }
        prev = curr;
        curr = curr->next;
    }
    return NULL;
}

void allocator_free(Allocator* allocator, void* memory) {
    Block* block = (Block*)((char*)memory - sizeof(Block));
    block->next = allocator->free_list;
    allocator->free_list = block;
}