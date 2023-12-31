/*
 *
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "NotTeam",
    /* First member's full name */
    "Name_Here",
    /* First member's email address */
    "notemail",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

//#define DEBUG
/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

//#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

static char *heap_listp = 0;
static char *global_list_start_ptr = 0;

/* ========================= Macros ========================= */

#define ALLOCATED 1
#define FREE 0

#define WORD_SIZE 	   	4   		// 4 bytes = word
#define DOUBLE_WORD_SIZE	8   		// 8 bytes = double word
#define MIN_BLOCK_SIZE		(4 * WORD_SIZE) // 16 bytes = bare minimum: header, footer, predecessor, successor
#define CHUNK_SIZE  	  	512 		// initial heap size

/* Align the block size, leaving space at the beginning and end, and return the actual allocated size */
#define ALIGN_SIZE(size) ((size) <= (DOUBLE_WORD_SIZE) ? MIN_BLOCK_SIZE : ALIGN(DOUBLE_WORD_SIZE + size))

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(ptr) 	(*(unsigned int *)(ptr))
#define PUT(ptr, val)	(*(unsigned int *)(ptr) = (unsigned int)val)

/* Get the block Size or Alloc information (stored in the header and footer) */
#define GET_SIZE(p)  (GET(p) & ~0x7) // zeroes out the last 3 bits
#define GET_ALLOC(p) (GET(p) & 0x1) // get the last bit

/* Get the header and footer addresses of block, given the block pointer */
#define GET_HEADER(bp) ((char *)(bp) - WORD_SIZE)
#define GET_FOOTER(bp) ((char *)(bp) + GET_SIZE(GET_HEADER(bp)) - DOUBLE_WORD_SIZE)

/* Get the previous or next block pointer physically right next to the current one, given the block pointer */
#define GET_NEXT_BLOCK(bp)     ((char *)(bp) + GET_SIZE(GET_HEADER(bp)))
#define GET_PREVIOUS_BLOCK(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DOUBLE_WORD_SIZE)))

/* Get the current, successor, or predecessor block size (information stored in the header of the block pointer) */
#define GET_CURRENT_BLOCK_SIZE(bp)   GET_SIZE(GET_HEADER(bp))
#define GET_NEXT_BLOCK_SIZE(bp)      GET_SIZE(GET_HEADER(GET_NEXT_BLOCK(bp)))
#define GET_PREVIOUS_BLOCK_SIZE(bp)  GET_SIZE(GET_HEADER(GET_PREVIOUS_BLOCK(bp)))

/* Get the predecessor and successor addresses, given the block pointer */
#define GET_PREDECESSOR(bp)	((char*)(bp) + WORD_SIZE)
#define GET_SUCCESSOR(bp) 	((char*)bp)

/* Get the predecessor and successor block pointer of the entire free block chain list, given the block pointer */
/* Having pointer to next free block means we have an "Explicit list among the free blocks using pointers" */
#define GET_FREE_LIST_PREDECESSOR_BLOCK(bp)	(GET(GET_PREDECESSOR(bp)))
#define GET_FREE_LIST_SUCCESSOR_BLOCK(bp) 	(GET(GET_SUCCESSOR(bp)))

/* Total number of large and small categories */

// Number of free list segregations, total number of size classes
#define NUM_OF_SIZE_CLASSES 15

/* 
      Diagram

A   : Allocated? (1: true, 0:false)
 
 < Allocated Block >
 
 
             31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
            +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 Header :   |                              size of the block                                       |  |  | A|
    bp ---> +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
            |                                                                                               |
            |                                                                                               |
            .                              Payload and padding                                              .
            .                                                                                               .
            .                                                                                               .
            +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 Footer :   |                              size of the block                                       |     | A|
            +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 
 
 < Free block >
 
             31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
            +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 Header :   |                              size of the block                                       |  |  | A|
    bp ---> +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
            |                        pointer to its successor in Segregated list                            |
bp+WSIZE--> +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
            |                        pointer to its predecessor in Segregated list                          |
            +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
            .                                                                                               .
            .                                                                                               .
            .                                                                                               .
            +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 Footer :   |                              size of the block                                       |     | A|
            +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

*/

/* ======================= FUNCTION DECLARATIONS ========================= */

/*** LIST UTILS ***/
static int  list_util_get_index(size_t);
static void list_util_insert_free_block(char *);
static void list_util_delete_free_block(char *);

/*** HEAP UTILS ***/
static void *heap_util_extend_heap(size_t);
static void *heap_util_coalesce(void *);
static void *heap_util_find_fit(size_t, int);
static void *heap_util_place(char *, size_t);

/* ======================= LIST UTILS ========================= */

/* 
 * list_util_get_index - Get the size class serial number (index) from the current block size of v (something like 1024, 2048, 4096, etc. bytes)
 *		 NOTE:   Size class returned could be any positive int from 0 to 15 (because that's the number of size classes we have)
 */
static int list_util_get_index(size_t v) 
{
	// log 2, O(1) time complexity of bitwise operations
	// Refer to 'Bit twiddling hacks' by Sean Anderson 
	// Linking: https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogLookup
    
	// v is what we're trying to find the log of
	size_t r, shift; // r == lg(v)
	r = (v > 0xFFFF)   << 4; v >>= r;
	shift = (v > 0xFF) << 3; v >>= shift; r |= shift;
	shift = (v > 0xF)  << 2; v >>= shift; r |= shift;
	shift = (v > 0x3)  << 1; v >>= shift; r |= shift;
                                              r |= (v >> 1);

	// Start from 2^4 (free block minimum 16 bytes)
	int sizeClassIndex = (int)r - 4;
	// if our size class index is less than 0, we'll just assume it's 0
	if (sizeClassIndex < 0)
	{
		sizeClassIndex = 0;
	}
	// if our size class index is greater than the number of size classes, then we'll assume it's the last size class index
	if (sizeClassIndex >= NUM_OF_SIZE_CLASSES)
	{
		sizeClassIndex = NUM_OF_SIZE_CLASSES - 1;
        }

	return sizeClassIndex;
}

/* 
 * list_util_insert_free_block - insert free block
 */
static void list_util_insert_free_block(char *freeBlockPointer)
{
    	// Get the size class in the free list to which the inserted free block belongs
    	int freeListSegregationSizeClassIndex = list_util_get_index(GET_CURRENT_BLOCK_SIZE(freeBlockPointer));
	char *root = global_list_start_ptr + (freeListSegregationSizeClassIndex * WORD_SIZE);

	// Address sorting - Address Order
	void *successorBlockPointer = root;
	
	// REMEMBER: PUT(ptr, val)
    	
    	// loop through every successor block in the free list until there are no more successor blocks in the free list
	while (GET_FREE_LIST_SUCCESSOR_BLOCK(successorBlockPointer))
	{
		// keep traversing through each successor block in the free list
		successorBlockPointer = (char *)GET_FREE_LIST_SUCCESSOR_BLOCK(successorBlockPointer);
		
		// we want to find a free block with an address larger than the one we're currently inserting (otherwise we insert at the largest address at the end)
		// if the successor block pointer we're using to traverse is greater than or equal to the free block pointer we're trying to insert
		if (((unsigned int)successorBlockPointer) >= ((unsigned int)freeBlockPointer))
		{
			// insert free block in address order
			
			// get predecessor block pointer from successor block pointer, which is the pointer we've been using to traverse the free list
			char *predecessorBlockPointer = (char *)GET_FREE_LIST_PREDECESSOR_BLOCK(successorBlockPointer);	// equivalent to: predecessorNode = successorNode->predecessor;
			PUT(GET_SUCCESSOR(predecessorBlockPointer), freeBlockPointer);					// equivalent to: predecessorNode->successor = nodeToInsert;
			PUT(GET_PREDECESSOR(freeBlockPointer), predecessorBlockPointer);				// equivalent to: nodeToInsert->predecessor = predecessorNode;
			PUT(GET_SUCCESSOR(freeBlockPointer), successorBlockPointer);					// equivalent to: nodeToInsert->successor = successorNode;
			PUT(GET_PREDECESSOR(successorBlockPointer), freeBlockPointer);					// equivalent to: nodeToInsert = successorNode->predecessor;
			return;
		}
	}
    
    	// Base Case & Last Case
    	// No free blocks for the current size class OR no free blocks during address allocation, the current free block address with the largest address is allocated at the end
    	PUT(GET_SUCCESSOR(successorBlockPointer), freeBlockPointer);	// equivalent to: successorNode->successor = nodeToInsert;
    	PUT(GET_PREDECESSOR(freeBlockPointer), successorBlockPointer);	// equivalent to: nodeToInsert->predecessor = successorNode;
    	PUT(GET_SUCCESSOR(freeBlockPointer), NULL);			// equivalent to: nodeToInsert->succesor = NULL;
}

/* 
 * list_util_delete_free_block - delete free block by unlinking all references to it
 */
static void list_util_delete_free_block(char *freeBlockPointer)
{
	// REMEMBER: PUT(ptr, val)
	
	// if there is a successor and a predecessor block
	if (GET_FREE_LIST_SUCCESSOR_BLOCK(freeBlockPointer) && GET_FREE_LIST_PREDECESSOR_BLOCK(freeBlockPointer))
	{
		// let the GET_SUCCESSOR() position of the predecessor of the node to be deleted be the successor of the node to be deleted
		// equivalent to:
		// nodeToBeDeleted->predecessor->successor = nodeToBeDeleted->successor;
		PUT(GET_SUCCESSOR(GET_FREE_LIST_PREDECESSOR_BLOCK(freeBlockPointer)), GET_FREE_LIST_SUCCESSOR_BLOCK(freeBlockPointer));
		
		
		// let the GET_PREDECESSOR() position of the successor of the node to be deleted be the predecessor of the node to be deleted
		// equivalent to:
		// nodeToBeDeleted->successor->predecessor = nodeToBeDeleted->predecessor;
		PUT(GET_PREDECESSOR(GET_FREE_LIST_SUCCESSOR_BLOCK(freeBlockPointer)), GET_FREE_LIST_PREDECESSOR_BLOCK(freeBlockPointer));
    	}
    	// if there is just a predecessor block because we're dealing with the last block (so there's no successor block)
    	else if (GET_FREE_LIST_PREDECESSOR_BLOCK(freeBlockPointer))
	{
		// let the GET_SUCCESSOR() position of the predecessor of the node to be deleted be NULL
		// equivalent to:
		// nodeToBeDeleted->predecessor->successor = NULL;
		PUT(GET_SUCCESSOR(GET_FREE_LIST_PREDECESSOR_BLOCK(freeBlockPointer)), NULL);
	}

	// finally, we unlink any pointer references to other blocks from our free block we're deleting
	// equivalent to:
	// nodeToBeDeleted->successor = NULL;
	// nodeToBeDeleted->predecessor = NULL;
	PUT(GET_SUCCESSOR(freeBlockPointer), NULL);
	PUT(GET_PREDECESSOR(freeBlockPointer), NULL);
}


/* ========================= FUNCTION ========================= */

/* 
 * mm_init - initialization
 *         - Initializes the heap like that shown below.
 *  ____________                                                    _____________
 * |  PROLOGUE  |                8+ bytes or 2 ptrs                |   EPILOGUE  |
 * |------------|------------|-----------|------------|------------|-------------|
 * |   HEADER   |   HEADER   |        PAYLOAD         |   FOOTER   |    HEADER   |
 * |------------|------------|-----------|------------|------------|-------------|
 * ^            ^            ^       
 * global_listp free_listp   heap_listp
 */
int mm_init(void)
{
	// Number of free list segregations (AKA total number of size classes) + 3 where the three comes from: the prolog header, the prolog footer, and the epilog header
	if ((heap_listp = mem_sbrk((NUM_OF_SIZE_CLASSES + 3) * WORD_SIZE)) == ((void *)-1))
    	{
		// allocation error
		return -1;
	}

	int i;
	// free block
	for (i = 0; i < NUM_OF_SIZE_CLASSES; ++i)
	{
		// Initialize free block size class header pointer
        	PUT((heap_listp + (i * WORD_SIZE)), NULL);
        }

	// Allocation block
	PUT((heap_listp + ((i + 0) * WORD_SIZE)), PACK(DOUBLE_WORD_SIZE, ALLOCATED));  /* Prolog block header */
	PUT((heap_listp + ((i + 1) * WORD_SIZE)), PACK(DOUBLE_WORD_SIZE, ALLOCATED));  /* Prolog block footer */
	PUT((heap_listp + ((i + 2) * WORD_SIZE)), PACK(0, ALLOCATED));      	       /* Epilog header */

	global_list_start_ptr = heap_listp;
	heap_listp += ((i + 1) * WORD_SIZE); // Align to start block payload, right after prolog

	/* Extend the empty stack to CHUNK_SIZE number of bytes */
	if (heap_util_extend_heap(CHUNK_SIZE) == NULL)
	{
		// Alloc Error
		return -1;
        }
	
	return 0;
}

/* 
 * mm_malloc - Allocation block, first-time adapation, and merge
 */
void *mm_malloc(size_t size)
{
	size_t alignedSize = ALIGN_SIZE(size);    /* Adjusted block size */
	size_t extendedSize;                  	  /* Amount to extend heap size by */
	char  *blockPointer;

	/* Trivial Case */
	if (size == 0)
	{
        	return NULL;
	}

	// Looking for a fit / finding an adaptation
	if ((blockPointer = heap_util_find_fit(alignedSize, list_util_get_index(alignedSize))) != NULL)
	{
        	return heap_util_place(blockPointer, alignedSize);
        }

    	/* No adaptation found, allocate more heap space */
    	extendedSize = MAX(alignedSize, CHUNK_SIZE);
    	if ((blockPointer = heap_util_extend_heap(extendedSize)) == NULL)
    	{
    		// Alloc Error
        	return NULL;
	}

	return heap_util_place(blockPointer, alignedSize);
}

/*
 * mm_free - Free the block and merge it immediately
 */
void mm_free(void *ptr)
{
	char *blockPointer = ptr;
	size_t blockSize = GET_CURRENT_BLOCK_SIZE(blockPointer);

	PUT(GET_HEADER(blockPointer), PACK(blockSize, FREE));
	PUT(GET_FOOTER(blockPointer), PACK(blockSize, FREE));
	heap_util_coalesce(blockPointer);
}

/*
 * mm_realloc - reallocate
 */
void *mm_realloc(void *ptr, size_t size)
{
	// if (ptr == NULL) AKA (!ptr), then perform direct allocation with size
	if (ptr == NULL)
	{
        	return mm_malloc(size);
	}
	// else if (size == 0) AKA (!size), then free the memory and return NULL
	else if (size == 0)
	{
		mm_free(ptr);
		return NULL;
	}
	
	size_t alignedSize = ALIGN_SIZE(size);			// size we want to allocate that is aligned
	size_t currentBlockSize = GET_CURRENT_BLOCK_SIZE(ptr);	// size of the original ptr

	// if the alignedSize of the new block we're trying to allocate with realloc() is the same size as the size of the current block pointer we're trying to allocate more memory for, then we
	// simply return the same pointer because we don't need to allocate more memory if the size we're supposed to be allocating is the same as the current size we have
	if (currentBlockSize == alignedSize)
	{
        	return ptr;
	}

	//size_t previousBlockAllocationStatus =  GET_ALLOC(GET_FOOTER(GET_PREVIOUS_BLOCK(ptr)));	// allocation status of previous memory block from ptr - 0 for free, 1 for allocated
	size_t nextBlockAllocationStatus =  GET_ALLOC(GET_HEADER(GET_NEXT_BLOCK(ptr)));		// allocation status of next memory block from ptr     - 0 for free, 1 for allocated
	size_t nextSize = GET_NEXT_BLOCK_SIZE(ptr);						// size of next memory block after ptr
	char *nextBlockPointer = GET_NEXT_BLOCK(ptr);						// pointer to the next block of memory after ptr

	/* =========================================== */
	// if the size of next block is 0 AKA (!nextSize) (meaning the next block, after the current block, is empty)
    	// then
    	// 1. extend the heap size by (the size we want to allocate) - (the size of the original ptr); that way we at least have enough bytes to place in what we need; error check for alloc error
    	// 2. total current block size now includes the extendedSize that we grew the heap by to match the amount of space needed
    	// 3. mark the new header and footer of ptr as allocated (since we changed ptr's total current block size)
    	// 4. mark the header of the next/end block as allocated until made otherwise
    	// 5. return the block pointer placed in with its new aligned size (since the aligned size we're trying to realloc() is greater than or equal to the current size)
    	if (nextSize == 0)
    	{
		size_t extendedSize = alignedSize - currentBlockSize;
		if (((long)(mem_sbrk(extendedSize))) == -1)
		{
			// Alloc Error
			return NULL;
		}
		
		currentBlockSize += extendedSize;
		
		PUT(GET_HEADER(ptr), PACK(currentBlockSize, ALLOCATED));
		PUT(GET_FOOTER(ptr), PACK(currentBlockSize, ALLOCATED));
		PUT(GET_HEADER(GET_NEXT_BLOCK(ptr)), PACK(0, ALLOCATED));
		
		return heap_util_place(ptr, alignedSize);
    	}
	// if 
	// 1. next block of original ptr is free AND 
	// 2. (size of original ptr block + size of next block) is greater than or equal to size we want to allocate
	// then
	// 1. total current block size now includes the size of the next block
	// 2. purge data (pointer links) from the next block
	// 3. mark the new header and footer of ptr as allocated (since we changed ptr's total current block size)
	// 4. return the block pointer placed in with its new total size
	if ((nextBlockAllocationStatus == FREE) && ((currentBlockSize + nextSize) >= alignedSize))
	{
		currentBlockSize += nextSize;
		list_util_delete_free_block(nextBlockPointer);
		
		PUT(GET_HEADER(ptr), PACK(currentBlockSize, ALLOCATED));
		PUT(GET_FOOTER(ptr), PACK(currentBlockSize, ALLOCATED));
		
		return heap_util_place(ptr, currentBlockSize);
    	}
    	// if
    	// 1. next block of original ptr is allocated OR
    	// 2. (size of original ptr block + size of next block) is less than the size we want to allocate
    	// AND
    	// 1. the size of the next block is 0 (we reached the end) OR
    	// 2. the size we want to allocate is less than the size of the original ptr
    	// SUMMARY: if aligned size (size we want to allocate) is smaller than the current block size (and there's no next block that is a free block to merge with)
    	// then
    	// 1. we create a newptr by calling malloc(), this one matches our aligned size, that is the size we want to realloc() (which should be smaller than ptr's old currentBlockSize from before)
    	// 2. if newptr is NULL because of an alloc error when extending the heap, then return NULL
    	// 3. void *memcpy(void *to, const void *from, size_t numBytes); so copy from the original ptr, to the newptr, for 
	else
	{
		char *newptr = mm_malloc(alignedSize);
		if (newptr == NULL)
		{
			return NULL;
		}
		memcpy(newptr, ptr, MIN(currentBlockSize, size));
		mm_free(ptr);
		return newptr;
	}
	
}

/* ======================= HEAP UTILS ========================= */

/* 
 * heap_util_extend_heap - Expand the heap, align size, perform merge, and return blockPointer
 */
static void *heap_util_extend_heap(size_t extendedSize)
{
	char *blockPointer;
	
	// expanding the heap size with mem_sbrk() by some extendedSize number of bytes and error checking if there's not enough heap space
	// mem_sbrk() returns "a generic pointer to the first byte of the newly allocated heap area"
    	if ((long)(blockPointer = mem_sbrk(extendedSize)) == -1)
    	{
        	// Alloc Error
        	return NULL;
    	}
    
	// Initialize the header and footer of the free block (marked as free); also initialize the header of the end block, the next block has size 0 and is allocated until made otherwise
	// PUT block of extended heap size marked as free into the header and footer of current block pointer
	PUT(GET_HEADER(blockPointer), PACK(extendedSize, FREE));                // free block header
	PUT(GET_FOOTER(blockPointer), PACK(extendedSize, FREE));         	// free block footer
	PUT(GET_HEADER(GET_NEXT_BLOCK(blockPointer)), PACK(0, ALLOCATED));	// end block header

	// perform merge and return the block pointer
    	return heap_util_coalesce(blockPointer);
}

/* 
 * heap_util_coalesce - Merge the blocks pointed to by the blockPointer before and after, and return blockPointer
 */
static void *heap_util_coalesce(void *blockPointer)
{
	size_t previousBlockAllocationStatus = GET_ALLOC(GET_FOOTER(GET_PREVIOUS_BLOCK(blockPointer))); // allocation status of previous memory block from ptr - 0 for free, 1 for allocated
	size_t nextBlockAllocationStatus = GET_ALLOC(GET_HEADER(GET_NEXT_BLOCK(blockPointer)));		// allocation status of next memory block from ptr     - 0 for free, 1 for allocated
	size_t blockSize = GET_CURRENT_BLOCK_SIZE(blockPointer);
	
	// REMEMBER: PUT (ptr, value)

	// NOTE: redundant if-statement
	// if the previous block is allocated AND the next block is allocated
	// then
	// 1. after the if statement, insert the free block pointer into the list
	// 2. after the if statement, return the block pointer
	/*
	if (previousBlockAllocationStatus && nextBlockAllocationStatus)
	{
		
	}
	*/
	// if the previous block is allocated AND the next block is free
	// then
	// 1. the blockSize will now expand to include the size of the next block (since it's free and we're absorbing it)
	// 2. purge data (pointer links) from the next block pointer (since it's free and we're absorbing it), that way we can use it
	// 3. PUT block of size we want to allocate marked as free into the header and footer of the current block pointer
	// 4. after the if statement, insert the free block pointer into the list
	// 5. after the if statement, return the block pointer
	if ((previousBlockAllocationStatus == ALLOCATED) && (nextBlockAllocationStatus == FREE))
    	{
		blockSize += GET_NEXT_BLOCK_SIZE(blockPointer);
		list_util_delete_free_block(GET_NEXT_BLOCK(blockPointer));
		
		PUT(GET_HEADER(blockPointer), PACK(blockSize, FREE));
		PUT(GET_FOOTER(blockPointer), PACK(blockSize, FREE));
	}
	// if the previous block is free AND the next block is allocated
	// then
	// 1. the blockSize will now expand to include the size of the previous block (since it's free and we're absorbing it)
	// 2. purge data (pointer links) from the previous block pointer (since it's free and we're absorbing it), that way we can use it
	// 3. PUT block of size we want to allocate marked as free into the footer of the current block pointer (this is our new footer)
	// 4. PUT block of size we want to allocate marked as free into the header of the previous block pointer (this is our new header)
	// 5. set the block pointer to begin at the address of the previous block (since we absorbed it)
	// 6. after the if statement, insert the free block pointer into the list
	// 7. after the if statement, return the block pointer
    	else if ((previousBlockAllocationStatus == FREE) && (nextBlockAllocationStatus == ALLOCATED))
    	{
		blockSize += GET_PREVIOUS_BLOCK_SIZE(blockPointer);
		list_util_delete_free_block(GET_PREVIOUS_BLOCK(blockPointer));

		PUT(GET_FOOTER(blockPointer), PACK(blockSize, FREE));
		PUT(GET_HEADER(GET_PREVIOUS_BLOCK(blockPointer)), PACK(blockSize, FREE));

		blockPointer = GET_PREVIOUS_BLOCK(blockPointer);
    	}
    	// if the previous block is free AND the next block is free
    	// then
    	// 1. the blockSize will now expand to include BOTH the size of the previous block and the next block (since both previous and next blocks are free and we're absorbing both blocks)
    	// 2. purge data (pointer links) from BOTH the previous block pointer and the next block pointer (since both previous and next blocks are free and we're absorbing both blocks)
    	// 3. PUT block of size we want to allocate marked as free into the header of the previous block pointer (this is our new header)
    	// 4. PUT block of size we want to allocate marked as free into the footer of the next block pointer (this is our new footer)
    	// 5. set the block pointer to begin at the address of the previous block (since we absorbed it)
	// 6. after the if statement, insert the free block pointer into the list
	// 7. after the if statement, return the block pointer
    	else if ((previousBlockAllocationStatus == FREE) && (nextBlockAllocationStatus == FREE))
    	{
		blockSize += GET_NEXT_BLOCK_SIZE(blockPointer) + GET_PREVIOUS_BLOCK_SIZE(blockPointer);
		list_util_delete_free_block(GET_PREVIOUS_BLOCK(blockPointer));
		list_util_delete_free_block(GET_NEXT_BLOCK(blockPointer));
		
		PUT(GET_HEADER(GET_PREVIOUS_BLOCK(blockPointer)), PACK(blockSize, FREE));
		PUT(GET_FOOTER(GET_NEXT_BLOCK(blockPointer)), PACK(blockSize, FREE));
		
		blockPointer = GET_PREVIOUS_BLOCK(blockPointer);
    	}
    	
    	list_util_insert_free_block(blockPointer);
	return blockPointer;
}

/* 
 * heap_util_find_fit - Find the adaptation, return the adapted free block pointer, and use the first adaptation
 */

static void *heap_util_find_fit(size_t blockSize, int freeListSegregationSizeClassIndex)
{
	// First Fit
	// loop thourgh every free list segregation class size until we've searched each to find the right--that is, the first--fit
	while (freeListSegregationSizeClassIndex < NUM_OF_SIZE_CLASSES)
    	{
		char *root = global_list_start_ptr + (freeListSegregationSizeClassIndex * WORD_SIZE);
		char *blockPointer = (char *)GET_FREE_LIST_SUCCESSOR_BLOCK(root);	// traversing block pointer starts at the block after the root
		// loop through every successor block pointer in the free list until we find one large enough to meet our size OR we reach a NULL block pointer
		while (blockPointer)
		{
			// if the size of the current block pointer is greater than or equal to the size we're trying to fit, we return that block pointer
			if ((size_t)GET_CURRENT_BLOCK_SIZE(blockPointer) >= blockSize)
			{
		        	return blockPointer;
		        }
		    	blockPointer = (char *)GET_FREE_LIST_SUCCESSOR_BLOCK(blockPointer);
		}
		// suitable block not found in this size class, search in a larger size class
		freeListSegregationSizeClassIndex++;
    	}
	
	// could not find a fit matching the size we wanted, return a NULL block pointer
	return NULL;
}

/* 
 * heap_util_place - place blocks, if the remaining part is larger than a minimum block size (16 bytes), then split the remaining part
 */
static void *heap_util_place(char *blockPointer, size_t alignedSize)
{
	size_t blockSize = GET_CURRENT_BLOCK_SIZE(blockPointer);	// size of current block pointer
	// the remaining size is literally how much space WOULD be left after we place the block
	size_t remainingSize = blockSize - alignedSize;			// remaining size = (size of current block pointer) - (size of block we want to allocate)
	list_util_delete_free_block(blockPointer);			// purge data (pointer links) from the current block pointer
	
	// REMEMBER: PUT (ptr, value)
	
	// if the remaining size after we would place the block is inseparable because the remainingSize is smaller than the MIN_BLOCK_SIZE (16 bytes)
    	// then
    	// 1. PUT block of size of current block pointer, marked as allocated, into the header and footer of the current block pointer
    	// 2. after the if statement, return the same current block pointer
	if (remainingSize < MIN_BLOCK_SIZE)
    	{
		PUT(GET_HEADER(blockPointer), PACK(blockSize, ALLOCATED));
		PUT(GET_FOOTER(blockPointer), PACK(blockSize, ALLOCATED));
	}
	// otherwise, if 
	// 1. the remaining space after we would place the block is larger than or equal to a minimum block size (16 bytes) BUT
	// 2. the size of the block we want to allocate (the alignedSize) is particularly small (smaller than 32 bytes in this case)
	// then
	// 1. PUT block of size we want to allocate (alignedSize), marked as allocated, into the header and footer of the current block pointer
	// 2. PUT block of remaining size, marked as free, into the header and footer of the next block pointer
	// 3. heap_util_coalesce() the fragments given the block pointer
	// 4. after the if statement, return the block pointer
	// split the remaining size
	else if ((remainingSize >= MIN_BLOCK_SIZE) && (alignedSize < 32))
	{
		// if the size of the block we want to allocate is smaller than or equal to 64 bytes
		// then
		PUT(GET_HEADER(blockPointer), PACK(alignedSize, ALLOCATED));
		PUT(GET_FOOTER(blockPointer), PACK(alignedSize, ALLOCATED));
		
		PUT(GET_HEADER(GET_NEXT_BLOCK(blockPointer)), PACK(remainingSize, FREE));
		PUT(GET_FOOTER(GET_NEXT_BLOCK(blockPointer)), PACK(remainingSize, FREE));

		heap_util_coalesce(GET_NEXT_BLOCK(blockPointer));
    	}
	// if 
	// 1. the remaining space after we would place the block is larger than or equal to a minimum block size (16 bytes) BUT
	// 2. the size of the block we want to allocate (the alignedSize) is particularly large (greater than at least 32 bytes in this case)
	// then
	// 1. PUT block of remaining size we want to allocate, marked as free, into the header and footer of current block pointer
	// 2. PUT block of size we want to allocate (alignedSize), marked as allocated, into the header and footer of the next block pointer
	// 3. merge the remaining blocks into the current block pointer with list_util_insert_free_block(), which inserts a block over a free block
	// 4. return the pointer to the block that was actually placed, which is the next block after the current block pointer
	else if ((remainingSize >= MIN_BLOCK_SIZE) && (alignedSize >= 32))
	{
		PUT(GET_HEADER(blockPointer), PACK(remainingSize, FREE));
		PUT(GET_FOOTER(blockPointer), PACK(remainingSize, FREE));
		PUT(GET_HEADER(GET_NEXT_BLOCK(blockPointer)), PACK(alignedSize, ALLOCATED));
		PUT(GET_FOOTER(GET_NEXT_BLOCK(blockPointer)), PACK(alignedSize, ALLOCATED));
		list_util_insert_free_block(blockPointer);	// merge the remaining blocks
		return GET_NEXT_BLOCK(blockPointer);	// returns the block that was actually placed
	}
	
	return blockPointer;
}











