/*
 * mm-explicit.c - The best malloc package EVAR!
 *er
 * TODO (bug): Uh..this is an implicit list???
 */

#include <stdint.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

/** The required alignment of heap payloads */
const size_t ALIGNMENT = 2 * sizeof(size_t);

// Abstract underlying data type for header and footer.
typedef size_t header_t, footer_t;

/** The layout of each block allocated on the heap */
typedef struct {
    /** The size of the previous block and whether it is allocated (stored in the low bit)
     */
    footer_t footer;
    /** The size of the block and whether it is allocated (stored in the low bit) */
    header_t header;
    /**
     * We don't know what the size of the payload will be, so we will
     * declare it as a zero-length array.  This allow us to obtain a
     * pointer to the start of the payload.
     */
    uint8_t payload[];
} block_t;

/** The layout of each free block in the free list (linked list). linked_node_t is part of
 * free block's payload. */
typedef struct linked_node_t {
    /** Pointer to previous free block */
    struct linked_node_t *prev;
    /** Pointer to next free block */
    struct linked_node_t *next;
} linked_node_t;

/** The head and tail blocks of the free list on the heap */
linked_node_t *head = NULL;
linked_node_t *tail = NULL;

/** Rounds up `size` to the nearest multiple of `n` */
static size_t round_up(size_t size, size_t n) {
    return (size + (n - 1)) / n * n;
}

/**
 * Sets the header and footer of a block with the given size and allocation status.
 * The header and footer are used to store metadata about
 * the block, including its size and whether it is allocated or free.
 *
 * @param block A pointer to the block whose boundaries are being set.
 * @param size The size of the payload for the block, not including the space
 *             required for the header and footer.
 * @param is_allocated A boolean value indicating the allocation status of the
 *                     block.
 *
 * @note The function assumes that the block pointer is aligned and the size
 *       is properly adjusted to include the header and footer.
 */
static void set_boundaries(block_t *block, size_t size, bool is_allocated) {
    block->header = size | is_allocated;
    footer_t *footer = (footer_t *) ((char *) block + ALIGNMENT + size);
    *footer = size | is_allocated;
}

/** Extracts a block's size from its header */
static size_t get_size(block_t *block) {
    return block->header & ~1;
}

/** Extracts previous block's  size from current block's footer */
static size_t get_prev_size(block_t *block) {
    return block->footer & -1;
}

/** Extracts previous block's allocation state from current block's footer */
static bool is_prev_allocated(block_t *block) {
    return block->footer & 1;
}

/** Extracts the next block's allocation state */
static bool is_next_allocated(block_t *block) {
    header_t *next_header =
        (header_t *) ((char *) block + ALIGNMENT + get_size(block) + sizeof(size_t));
    // Note: get_size(block) gets size of payload
    if ((*next_header & ~1) !=
        0) { // if size of next block is not 0 (masks out allocation bit (last bit) rest
             // give size and sees if thats not 0)
        // *header & 00...01, masks out rest and returns allocation bit
        return (*next_header & 1); // check least significant bit of header to determine
                                   // if block is allocated
    }
    else {
        return true; // If size of next block is 0, then we are at end of heap (just a
                     // header that signifies end of memory)
    }
}

/** Gets the header corresponding to a given payload pointer */
static block_t *block_from_payload(void *ptr) {
    return ptr - offsetof(block_t, payload);
}

/** Adds linked_node_t to the block's payload */
/** Adds linked_node_t to the block's payload */
static void add_linked_node_to_block(block_t *block) {
    // Calculate the address for the new free block node by offsetting from the block
    // pointer
    linked_node_t *new_node = (linked_node_t *) ((char *) block + ALIGNMENT);
    // Set the new node's previous pointer to the current last node in the free list
    // (before the tail)
    new_node->prev = tail->prev;
    // Set the new node's next pointer to the tail, since its now the last node before the
    // tail
    new_node->next = tail;
    // Update the tail's previous node's next pointer to the new node, inserting it into
    // the list
    tail->prev->next = new_node;
    // Set the tail's previous pointer to the new node, since its now the new last node
    tail->prev = new_node;
}

/** Removes the linked_node_t in the block's payload */
static void remove_linked_node_from_block(block_t *block) {
    linked_node_t *temp = (linked_node_t *) ((char *) block + ALIGNMENT);
    // Set next of prev free node to next of curr free node
    (temp->prev)->next = temp->next;
    // Set prev of next free node to prev of curr free node
    (temp->next)->prev = temp->prev;
}

/**
 * Splits a given block into two parts and updates the free list.
 *
 * This function takes a block of memory and splits it into two separate blocks:
 * one that is allocated with the specified size and another that remains free.
 * The headers and footers of both blocks are set accordingly to reflect their
 * new sizes and allocation status. The original block is removed from the free
 * list, and the newly created free block is added to the free list.
 *
 * @param block A pointer to the block to be split. This block should be free
 *              and large enough to be split into two parts.
 * @param size The total size of the block being split.
 * @param allocated_size The size of the payload for the allocated part of the block after
 *             the split. This size must be less than the block_size and must
 *             accommodate the space for the block's header and footer.
 *
 * @note The block is assumed to be properly aligned and the size parameters are
 *       assumed to be multiples of the alignment requirement.
 */
static void split(block_t *block, size_t size, size_t allocated_size) {
    // Current block set to allocated with allocated payload size given to user
    set_boundaries(block, allocated_size, true);
    // Get next block
    block_t *da_next_block = (block_t *) ((char *) block + get_size(block) + ALIGNMENT);
    // Sets header and fooder of next block, which is the remainder of the original block
    // After splitting, this block is marked as free
    set_boundaries(da_next_block, size - allocated_size - ALIGNMENT, false);
    // Append a new free block (from the split) to the free list
    add_linked_node_to_block(da_next_block);
    // Remove linked node from free list that was in current block, since it is now
    // allocated
    remove_linked_node_from_block(block);
}

/**
 * Coalesces a block with adjacent free blocks if possible.
 *
 * This function attempts to merge a free block with its neighboring free blocks
 * to the left and right in the heap. If the previous or next block is free, it
 * merges the current block with it.
 * This process of coalescing helps to reduce fragmentation and creates larger free
 * blocks, making it easier to satisfy future allocation requests.
 *
 * @param block A pointer to the current block that we want to coalesce.
 * @param size The size of the current block.
 */
static void coalesce(block_t *block) {
    size_t size = get_size(block);
    // Check if the previous block is free and coalesce with it if it is
    if (!is_prev_allocated(block)) {
        // Increase the size by the size of the previous block and the alignment
        // (header/footer)
        size += get_prev_size(block) + ALIGNMENT;
        // Set the new size of the previous block after coalescing
        set_boundaries((block_t *) ((char *) block - get_prev_size(block) - ALIGNMENT),
                       size, false);
        // Remove the current block from the free list as it is now part of the previous
        // block
        remove_linked_node_from_block(block);

        // After coalescing with previous, check if we can also coalesce with the next
        // block
        if (!is_next_allocated(block)) {
            block_t *da_next_block =
                (block_t *) ((char *) block + get_size(block) + ALIGNMENT);
            // Increase the size by the size of the next block and alignment
            // (header/footer)
            size += get_size(da_next_block) + ALIGNMENT;
            // Set the new size of the coalesced block (which now includes the next block)
            set_boundaries(
                (block_t *) ((char *) block - get_prev_size(block) - ALIGNMENT), size,
                false);
            // Remove the next block from the free list as it is now part of the coalesced
            // block
            remove_linked_node_from_block(da_next_block);
            // Exit the function as coalescing is complete
            return;
        }
        // Exit the function if only coalescing with the previous block
        return;
    }
    // If the previous block is not free or has been handled, check the next block.
    if (!is_next_allocated(block)) {
        block_t *da_next_block =
            (block_t *) ((char *) block + get_size(block) + ALIGNMENT);
        size += get_size(da_next_block) + ALIGNMENT;
        set_boundaries(block, size, false);
        remove_linked_node_from_block(da_next_block);
    }
}

/**
 * Finds the first free block in the heap with at least the given size.
 * If no block is large enough, returns NULL.
 */
static block_t *find_fit(size_t size) {
    // Traverse the blocks in the heap using the explicit list
    // Iterate backwards from last block
    for (linked_node_t *curr = tail->prev; curr != head; curr = curr->prev) {
        // Retrieve the block_t structure from the current free_node
        block_t *free_block = (block_t *) ((char *) curr - ALIGNMENT);
        // Determine the size of the current free block
        size_t block_size = get_size(free_block);
        // Check if the current block is large enough to satisfy the allocation request
        if (block_size >= size) {
            // Set the block as allocated by updating its header and footer
            set_boundaries(free_block, block_size, true);
            // Check if the remaining space after allocation is too small to create a new
            // free block
            if (block_size < size + ALIGNMENT + 0.01) { // +0.01 somehow fixes the error
                remove_linked_node_from_block(free_block);
                return (free_block);
            }
            // If there's enough space, split the current block into an allocated block
            // and a new free block
            split(free_block, block_size, size);
            return free_block;
        }
    }
    return NULL;
}

/**
 * mm_init - Initializes the allocator state
 */
bool mm_init(void) {
    // Allocate space for the head and tail node of the free list using mem_sbrk,
    // which extends the heap by the size of ALIGNMENT
    head = (linked_node_t *) mem_sbrk(ALIGNMENT);
    tail = (linked_node_t *) mem_sbrk(ALIGNMENT);
    // Check if the heap extension failed
    if ((head == (void *) -1) || (tail == (void *) -1)) {
        return false;
    }
    // Initialize head and tail pointers next and prev's
    // NULL -> head -> tail -> NULL
    head->prev = NULL;
    head->next = tail;
    tail->prev = head;
    tail->next = NULL;

    // Allocated footer of prologue and header of epilogue of heap (boundaries)
    footer_t *prologue = (footer_t *) mem_sbrk(sizeof(size_t));
    header_t *epilogue = (header_t *) mem_sbrk(sizeof(size_t));
    if ((prologue == (void *) -1) || (epilogue == (void *) -1)) {
        return false;
    }
    *prologue =
        0 |
        true; // Mark the start of the heap as used. (Sets allocated bit to 1, rest are 0)
    *epilogue = 0 | true; // Mark the end of the heap as used.

    return true;
}

/**
 * mm_malloc - Allocates a block with the given size
 */
void *mm_malloc(size_t size) {
    // Round up the requested size to meet the alignment requirements
    size = round_up(size, ALIGNMENT);
    // Try to find a free block that fits the rounded-up size
    block_t *block = find_fit(size);
    // If a fitting block is found, return the payload address
    if (block != NULL) {
        return block->payload;
    }
    // If no fitting block is found, extend the heap by the requested size
    // Adjust by ALIGNMENT to leave room for the header
    void *set_location = mem_sbrk(size) - ALIGNMENT;
    // Further extend the heap to make room for the footer
    footer_t *footer = (footer_t *) mem_sbrk(sizeof(footer_t));
    // Check if heap extension was successful, return NULL if not
    if (footer == (void *) -1) {
        return NULL;
    }
    // Set the extended heap space to block
    block = (block_t *) set_location;
    // Set the boundaries of the new block (header and footer) with the given size and
    // mark it as allocated
    set_boundaries(block, size, true);
    // Extend the heap to add an epilogue header which marks the end of the heap
    header_t *epilogue = (header_t *) mem_sbrk(sizeof(header_t));
    // Set the epilogue header with size 0 and mark it as allocated
    *epilogue = 0 | true;
    // Return the payload address of the allocated block (allocated memory for user)
    return block->payload;
}

/**
 * mm_free - Releases a block to be reused for future allocations
 */
void mm_free(void *ptr) {
    // mm_free(NULL) does nothing
    if (ptr == NULL) {
        return;
    }
    block_t *block = block_from_payload(ptr);
    // Mark allocation as false
    set_boundaries(block, get_size(block), false);
    // Add linked node to block's payload, indicating that it is free
    add_linked_node_to_block(block);
    // Coalesce prev and next of curr, coalesce function colesces neighboring blocks
    if (!is_prev_allocated(block) || !is_next_allocated(block)) {
        coalesce(block);
    }
}

/**
 * mm_realloc - Change the size of the block by mm_mallocing a new block,
 *      copying its data, and mm_freeing the old block.
 */
void *mm_realloc(void *old_ptr, size_t size) {
    if (old_ptr == NULL) {
        return (mm_malloc(size));
    }
    if (size == 0) {
        mm_free(old_ptr);
        return (NULL);
    }

    void *new_ptr = mm_malloc(size);
    size_t old_size = get_size(block_from_payload(old_ptr));
    size_t copy_size = old_size < size ? old_size : size;
    memcpy(new_ptr, old_ptr, copy_size);
    mm_free(old_ptr);
    return (new_ptr);
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
