#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    size_t length;
    int free_flag;
} Chunk;

typedef struct {
    Chunk* first_chunk;
    size_t total_capacity;
} AllocatorMcKusick;

AllocatorMcKusick* allocator_create(void* region, size_t region_size) {
    AllocatorMcKusick* handle = (AllocatorMcKusick*)region;
    handle->first_chunk = (Chunk*)((char*)region + sizeof(AllocatorMcKusick));
    handle->first_chunk->length = region_size - sizeof(AllocatorMcKusick) - sizeof(Chunk);
    handle->first_chunk->free_flag = 1;
    handle->total_capacity = region_size;
    return handle;
}

void allocator_destroy(AllocatorMcKusick* handle) {
    (void)handle;
}

void* allocator_alloc(AllocatorMcKusick* handle, size_t user_size) {
    Chunk* curr = handle->first_chunk;
    while ((char*)curr < (char*)handle->first_chunk + handle->total_capacity) {
        if (curr->free_flag && curr->length >= user_size) {
            if (curr->length - user_size > sizeof(Chunk)) {
                char* split_addr = (char*)curr + sizeof(Chunk) + user_size;
                Chunk* new_chunk = (Chunk*)split_addr;

                new_chunk->length = curr->length - user_size - sizeof(Chunk);
                new_chunk->free_flag = 1;
                curr->length = user_size;
            }
            curr->free_flag = 0;
            return (char*)curr + sizeof(Chunk);
        }
        curr = (Chunk*)((char*)curr + sizeof(Chunk) + curr->length);
    }
    return NULL;
}

void allocator_free(AllocatorMcKusick* handle, void* ptr) {
    if (!ptr) return;

    Chunk* blk = (Chunk*)((char*)ptr - sizeof(Chunk));
    blk->free_flag = 1;

    Chunk* curr = handle->first_chunk;
    while ((char*)curr < (char*)handle->first_chunk + handle->total_capacity) {
        char* next_addr = (char*)curr + sizeof(Chunk) + curr->length;
        if (next_addr >= (char*)handle->first_chunk + handle->total_capacity) {
            break;
        }
        Chunk* nxt = (Chunk*)next_addr;
        if (curr->free_flag && nxt->free_flag) {
            curr->length += sizeof(Chunk) + nxt->length;
            continue;
        }
        curr = nxt;
    }
}
