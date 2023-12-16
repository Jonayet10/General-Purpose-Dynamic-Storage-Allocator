/*
 * mm-implicit.c - The best malloc package EVAR!
 *
 */

#include <stdint.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

/** The required alignment of heap payloads */
const size_t ALIGNMENT = 2 * sizeof(size_t);

/** The layout of each block allocated on the heap */
typedef struct {
    /** The size of the block and whether it is allocated (stored in the low bit) */
    size_t header;
    /**
     * We don't know what the size of the payload will be, so we will
     * declare it as a zero-length array.  This allow us to obtain a
     * pointer to the start of the payload.
     */
    uint8_t payload[];
} block_t;

/** The first and last blocks on the heap */
static block_t *mm_heap_first = NULL;
static block_t *mm_heap_last = NULL;

/** Rounds up `size` to the nearest multiple of `n` */
static size_t round_up(size_t size, size_t n) {
    return (size + (n - 1)) / n * n;
}

/** Set's a block's header with the given size and allocation state */
static void set_header(block_t *block, size_t size, bool is_allocated) {
    block->header = size | is_allocated;
}

/** Extracts a block's size from its header */
static size_t get_size(block_t *block) {
    return block->header & ~1;
}

/** Extracts a block's allocation state from its header */
static bool is_allocated(block_t *block) {
    return block->header & 1;
}

/** Gets the header corresponding to a given payload pointer */
static block_t *block_from_payload(void *ptr) {
    return ptr - offsetof(block_t, payload);
}

/**
 * mm_init - Initializes the allocator state
 */
bool mm_init(void) {
    // We want the first payload to start at ALIGNMENT bytes from the start of the heap
    void *padding = mem_sbrk(ALIGNMENT - sizeof(block_t));
    if (padding == (void *) -1) {
        return false;
    }

    // Initialize the heap with no blocks
    mm_heap_first = NULL;
    mm_heap_last = NULL;
    return true;
}

/**
 * mm_malloc - Allocates a block with the given size
 */
void *mm_malloc(size_t size) {
    // The block must have enough space for a header and be 16-byte aligned
    size = round_up(sizeof(block_t) + size, ALIGNMENT);
    size_t required_size = size;

    // If there are no blocks yet, create the initial heap
    if (mm_heap_first == NULL) {
        block_t *block = mem_sbrk(required_size);
        if (block == (void *) -1) {
            return NULL;
        }
        set_header(block, required_size, true);
        mm_heap_first = block;
        mm_heap_last = block;
        return block->payload;
    }

    // Otherwise, traverse the heap to find a fit, coalescing adjacent free blocks along
    // the way
    block_t *curr = mm_heap_first;
    block_t *prev_free = NULL;
    while (curr <= mm_heap_last) {
        size_t curr_size = get_size(curr);
        // Check for coalescing with the previous free block
        if (!is_allocated(curr) && prev_free) {
            // Coalesce current block with previous free block
            curr_size += get_size(prev_free);
            set_header(prev_free, curr_size, false);
            curr = prev_free;
        }
        // Check if the current block is a fit
        if (!is_allocated(curr) && curr_size >= required_size) {
            // If the current block can be split
            if (curr_size - required_size >= (sizeof(block_t) + ALIGNMENT)) {
                set_header(curr, required_size, true);
                block_t *next_block =
                    (block_t *) ((char *) curr + required_size); // char * allows bytewise
                                                                 // pointer arithmetic
                set_header(next_block, curr_size - required_size, false);
                if (curr == mm_heap_last) {
                    mm_heap_last = next_block;
                }
            }
            else {
                // Allocate the whole block
                set_header(curr, curr_size, true);
            }
            return curr->payload;
        }
        // Update prev_free pointer if current block is free
        if (!is_allocated(curr)) {
            prev_free = curr;
        }
        else {
            prev_free = NULL;
        }
        // Move to the next block
        curr = (block_t *) ((char *) curr + curr_size);
    }
    // No fit found. Get more memory and place the block
    block_t *new_block = mem_sbrk(required_size);
    if (new_block == (void *) -1) {
        return NULL;
    }
    set_header(new_block, required_size, true);
    mm_heap_last = new_block;
    return new_block->payload;
}

/**
 * mm_free - Releases a block to be reused for future allocations
 */
void mm_free(void *ptr) {
    // mm_free(NULL) does nothing
    if (ptr == NULL) {
        return;
    }

    // Mark the block as unallocated
    block_t *block = block_from_payload(ptr);
    set_header(block, get_size(block), false);
}

/**
 * mm_realloc - Change the size of the block by mm_mallocing a new block,
 *      copying its data, and mm_freeing the old block.
 */
void *mm_realloc(void *old_ptr, size_t size) {
    if (old_ptr == NULL) {
        return mm_malloc(size);
    }

    if (size == 0) {
        mm_free(old_ptr);
        return NULL;
    }

    block_t *old_block = block_from_payload(old_ptr);
    // get_size old block retrieves size of old block including header
    // sizeof(block_t) gets size of header, which is subtracted from total size to get
    // size of payload
    size_t old_size = get_size(old_block) - sizeof(block_t);

    // If the size is the same, just return the old pointer
    if (size == old_size) {
        return old_ptr;
    }

    void *new_ptr = mm_malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    // Copy the data from the old block to the new block
    size_t copy_size = old_size < size ? old_size : size;
    memcpy(new_ptr, old_ptr, copy_size);

    // Free the old block
    mm_free(old_ptr);

    return new_ptr;
}

/**
 * mm_calloc - Allocate the block and set it to zero.
 */
void *mm_calloc(size_t nmemb, size_t size) {
    size_t total_size = nmemb * size;
    void *allocated = mm_malloc(total_size);
    if (allocated != NULL) {
        memset(allocated, 0, total_size);
    }
    return allocated;
}

/**
 * mm_checkheap - So simple, it doesn't need a checker!
 */
void mm_checkheap(void) {
}
