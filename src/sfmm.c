/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include "moreFunc.h"
#include <errno.h>

int first_mem_grow = 0;

int empty_list(sf_block dummy_blk){
    sf_block nxt_ptr= *dummy_blk.body.links.next;
    if(nxt_ptr.prev_footer == dummy_blk.prev_footer &&
        nxt_ptr.header == dummy_blk.header &&
        nxt_ptr.body.links.next == dummy_blk.body.links.next &&
        nxt_ptr.body.links.prev == dummy_blk.body.links.prev)
        return 1;
    else
        return 0;
}

size_t max_mem_of_list(sf_block dummy_blk){
    return (dummy_blk.body.links.prev->header & BLOCK_SIZE_MASK);
}

struct sf_block* find_smallest_list(sf_block* blk){
    int size = blk->header & BLOCK_SIZE_MASK;
    switch (size){
        case 32:
            return &sf_free_list_heads[0];
            break;
        case 33 ... 64:
            return &sf_free_list_heads[1];
            break;
        case 65 ... 128:
            return &sf_free_list_heads[2];
            break;
        case 129 ... 256:
            return &sf_free_list_heads[3];
            break;
        case 257 ... 512:
            return &sf_free_list_heads[4];
            break;
        case 513 ... 1024:
            return &sf_free_list_heads[5];
            break;
        case 1025 ... 2048:
            return &sf_free_list_heads[6];
            break;
        case 2049 ... 4096:
            return &sf_free_list_heads[7];
            break;
        default:
            return &sf_free_list_heads[8];
            break;
    }
}

void *sf_malloc(size_t size) {
    if(size == 0 || size < 0)
        return NULL;

    // if this is the first call to sf_malloc, initialize heap and add 4096 byte block to last list
    if(first_mem_grow == 0){
        sf_mem_grow();

        void* prol_start = sf_mem_start();
        void* bigblk_start = sf_mem_start() + 32;
        void* bigblk_footer = sf_mem_end() - 16;
        void* epi_start = sf_mem_end() - 8;

        //calculate inital prologue padding
        int padding = 0;
        if((uintptr_t)prol_start % 16 != 0){
            padding = 16 - (uintptr_t)prol_start%16;
        }

        //initialize prologue and set it into the heap
        sf_prologue prologue = {};
        sf_prologue* prol_ptr = &prologue;
        prol_ptr = ((struct sf_prologue*)prol_start);
        prol_ptr->padding1 = padding;
        prol_ptr->header = 0x00000023;
        prol_ptr->footer = prol_ptr->header ^ sf_magic();

        //add padding1 to end of prologue before initialzing free blocks and find header for big block
        sf_header blk_hdr = 0x00000FD1 - prol_ptr->padding1;
        bigblk_start += prol_ptr->padding1;

        //initialize block
        sf_block bigblk = {};
        sf_block* bigblk_ptr = &bigblk;
        bigblk_ptr = ((struct sf_block*)bigblk_start);
        bigblk_ptr->prev_footer = prol_ptr->footer;
        bigblk_ptr->header = blk_hdr;

        //Create a footer for the last block in heap
        sf_footer ftr;
        sf_footer* bigblk_footer_ptr = &ftr;
        bigblk_footer_ptr = (sf_footer*)bigblk_footer;
        *bigblk_footer_ptr = bigblk_ptr->header ^ sf_magic();

        //initialize free_lists
        for(int i = 0; i < NUM_FREE_LISTS; i++){
            sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
            sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        }

        //add block to last free_list
        bigblk_ptr->body.links.next = &sf_free_list_heads[7];
        bigblk_ptr->body.links.prev = &sf_free_list_heads[7];
        sf_free_list_heads[7].body.links.next = bigblk_ptr;
        sf_free_list_heads[7].body.links.prev = bigblk_ptr;

        //initialize epilogue and set it to the heap
        sf_epilogue epilogue = {};
        sf_epilogue* epi_ptr = &epilogue;
        epi_ptr = ((struct sf_epilogue*)epi_start);
        epi_ptr->header = 0xA;

        first_mem_grow = 1;
    }

    //find list with the smallest block sizes that are >= required memory
    int need_more_space_bool = 1;
    int required_mem = size + 16;
    if(size%16!=0)
        required_mem += (16 - size%16);
    sf_block *blk_to_allocate;
    int i = 0;
    while(i<NUM_FREE_LISTS){
        sf_block dummy_ptr = sf_free_list_heads[i];
        if((max_mem_of_list(dummy_ptr) >= required_mem) && (empty_list(dummy_ptr) == 0)){
            blk_to_allocate = dummy_ptr.body.links.next;
            // iterate until we reach the dummy node again or find a block of memory big enough
            while(blk_to_allocate->prev_footer != dummy_ptr.prev_footer &&
                blk_to_allocate->header != dummy_ptr.header &&
                blk_to_allocate->body.links.next != dummy_ptr.body.links.next &&
                blk_to_allocate->body.links.prev != dummy_ptr.body.links.prev){
                if((blk_to_allocate->header & BLOCK_SIZE_MASK) >= size){
                    i=8;        // ends while loop
                    need_more_space_bool = 0;
                    break;
                }
                else
                    blk_to_allocate = blk_to_allocate->body.links.next;
            }
        }
        i++;
    }
    //we either have to expand the heap or continue allocating the block

    //heap expansion
    if(need_more_space_bool == 1){

        //check whether the last mem block in the heap is allocated or not
        //if it's not allocated then we can expand this block(same effect as coalescing a new block)
        //if it is allocated, then just create a new block out of the extended heap
        void* coal_check_ptr = sf_mem_end() - 16;
        sf_footer coal_check_ftr;
        sf_footer* coal_check_ftr_ptr = &coal_check_ftr;
        *coal_check_ftr_ptr = *((sf_footer*)(coal_check_ptr)) ^ sf_magic();
        int bool_coal = 0;
        if((*coal_check_ftr_ptr & 0x2) == 0)
            bool_coal = 1;

        //relevant address pointer for extension
        void* newblk_start = sf_mem_grow();
        if(newblk_start == 0){
            sf_errno = ENOMEM;
            return NULL;
        }
        void* newblk_ftr_location = sf_mem_end() - 16;
        void* epi_start = sf_mem_end() - 8;
        void* prev_blk_ftr_ptr = newblk_start - 16;
        void* prev_blk_ptr = newblk_start - 8 - 8 - (*coal_check_ftr_ptr & BLOCK_SIZE_MASK);
        sf_block* prev_blk = (sf_block*)prev_blk_ptr;
        prev_blk->prev_footer = prev_blk->prev_footer ^ sf_magic();

        if(bool_coal == 0){

            //initialize block
            sf_block newblk = {};
            sf_block* newblk_ptr = &newblk;
            newblk_ptr = ((struct sf_block*)newblk_start);
            newblk_ptr->prev_footer = *((sf_footer*)(prev_blk_ftr_ptr)) ^ sf_magic();
            newblk_ptr->header = 0x000000FF0 | 0x1;

            //Create a footer for the last block in heap
            sf_footer newblk_ftr;
            sf_footer* newblk_ftr_ptr = &newblk_ftr;
            newblk_ftr_ptr = (sf_footer*)newblk_ftr_location;
            *newblk_ftr_ptr = newblk_ptr->header ^ sf_magic();

            //add block to last free_list
            sf_block* sentinal = find_smallest_list(newblk_ptr);
            sf_block* sentinal_prev = sentinal->body.links.prev;
            newblk_ptr->body.links.next = sentinal;
            newblk_ptr->body.links.prev = sentinal_prev;
            sentinal->body.links.prev = newblk_ptr;
            sentinal_prev->body.links.next = newblk_ptr;

            //initialize epilogue and set it to the heap
            sf_epilogue epilogue = {};
            sf_epilogue* epi_ptr = &epilogue;
            epi_ptr = ((struct sf_epilogue*)epi_start);
            epi_ptr->header = 0xA;
        }

        // "COALESCING"/EXPANDING
        if(bool_coal == 1){

            //free blk from list
            sf_block* prev_blk_prev = prev_blk->body.links.prev;
            sf_block* prev_blk_next = prev_blk->body.links.next;
            prev_blk->body.links.prev = NULL;
            prev_blk->body.links.next = NULL;
            prev_blk_prev->body.links.next = prev_blk_next;
            prev_blk_next->body.links.prev = prev_blk_prev;

            //edit header
            int prev_blk_allocated = prev_blk->header & 0x00000001;
            prev_blk->header = (prev_blk->header & BLOCK_SIZE_MASK) + 4096 + prev_blk_allocated;
            prev_blk->prev_footer = prev_blk->prev_footer ^ sf_magic();

            //Create a footer for the last block in heap
            sf_footer newblk_ftr;
            sf_footer* newblk_ftr_ptr = &newblk_ftr;
            newblk_ftr_ptr = (sf_footer*)newblk_ftr_location;
            *newblk_ftr_ptr = prev_blk->header ^ sf_magic();

            // //add block to last free_list
            sf_block* sentinal = find_smallest_list(prev_blk);
            sf_block* sentinal_prev = sentinal->body.links.prev;
            prev_blk->body.links.next = sentinal;
            prev_blk->body.links.prev = sentinal_prev;
            sentinal->body.links.prev = prev_blk;
            sentinal_prev->body.links.next = prev_blk;

            // //initialize epilogue and set it to the heap
            sf_epilogue epilogue = {};
            sf_epilogue* epi_ptr = &epilogue;
            epi_ptr = ((struct sf_epilogue*)epi_start);
            epi_ptr->header = 0xA;
        }

        return sf_malloc(size);
    }

    //continuing allocating, skipping heap extension

    //relevant heap addresses pertaining to the block to be allocated
    void* blk_hdr_addr = &blk_to_allocate->prev_footer;
    void* blk_end_addr = blk_hdr_addr + (blk_to_allocate->header & BLOCK_SIZE_MASK);

    //free blk from list
    sf_block* prev_blk = blk_to_allocate->body.links.prev;
    sf_block* next_blk = blk_to_allocate->body.links.next;
    blk_to_allocate->body.links.prev = NULL;
    blk_to_allocate->body.links.next = NULL;
    prev_blk->body.links.next = next_blk;
    next_blk->body.links.prev = prev_blk;

    //find splitting amount
    int bool_split = 1;
    int split_amt = required_mem;
    if(blk_to_allocate->header - required_mem < 32){
        split_amt = blk_to_allocate->header;
        bool_split = 0;
    }
    int splitter_blk_header_sz = (blk_to_allocate->header & BLOCK_SIZE_MASK) - split_amt;

    //edit block to be allocated
    blk_to_allocate->header = split_amt;
    blk_to_allocate->header = blk_to_allocate->header | 0x3;

    //create splitter block if we're not using the whole block
    if(bool_split == 1){
        sf_block split_blk = {};
        sf_block* split_blk_ptr = &split_blk;
        split_blk_ptr = ((struct sf_block*)(blk_hdr_addr + split_amt));
        split_blk_ptr->prev_footer = blk_to_allocate->header ^ sf_magic();
        split_blk_ptr->header = splitter_blk_header_sz | 0x1;

        //Create a footer for the last block in heap
        sf_footer ftr;
        sf_footer* split_blk_footer_ptr = &ftr;
        split_blk_footer_ptr = ((sf_footer*)blk_end_addr);
        *split_blk_footer_ptr = split_blk_ptr->header ^ sf_magic();

        //find where to place new block in free list
        sf_block* sentinal = find_smallest_list(split_blk_ptr);
        sf_block* sentinal_prev = sentinal->body.links.prev;
        split_blk_ptr->body.links.next = sentinal;
        split_blk_ptr->body.links.prev = sentinal_prev;
        sentinal->body.links.prev = split_blk_ptr;
        sentinal_prev->body.links.next = split_blk_ptr;
    }

    return blk_hdr_addr;
}

void sf_free(void *pp) {

    sf_block* blk = (sf_block*)pp;
    void* prev_loctn = pp - ((*(sf_footer*)(pp) ^ sf_magic()) & BLOCK_SIZE_MASK);
    void* nex_loctn = pp + (blk->header & BLOCK_SIZE_MASK);
    sf_block* prev_blk = (sf_block*)prev_loctn;
    sf_block* nex_blk = (sf_block*)nex_loctn;
    void* nex_nex_loctn = nex_loctn + (nex_blk->header & BLOCK_SIZE_MASK);
    sf_block* nex_nex_blk = (sf_block*)nex_nex_loctn;
    int bool_prv_coalesced = 0;

    //error handling
    if(pp == NULL || ((blk->header & 0x2) == 0) || (void *)&blk->header < sf_mem_start() ||
        (void *)&blk->prev_footer > sf_mem_end() || (blk->header & BLOCK_SIZE_MASK) < 32 ||
        (((blk->header & 0x1) == 0) && ((prev_blk->header & 0x2) != 0)) || (blk->header != (nex_blk->prev_footer ^ sf_magic())))
        abort();


    //set allocated bit to 0
    blk->header = blk->header & 0xFFFFFFFD;
    nex_blk-> prev_footer = blk->header ^ sf_magic();

    //add blk to FRONT of free list
    sf_block* sentinal = find_smallest_list(blk);
    sf_block* sentinal_next = sentinal->body.links.next;
    blk->body.links.next = sentinal_next;
    blk->body.links.prev = sentinal;
    sentinal_next->body.links.prev = blk;
    sentinal->body.links.next = blk;

    //if prev block is free then coalesce
    if((prev_blk->header & 0x2) == 0){
        bool_prv_coalesced = 1;
        int prv_alc = prev_blk->header & 0x1;
        prev_blk->header = (prev_blk->header & BLOCK_SIZE_MASK) + (blk->header & BLOCK_SIZE_MASK) + prv_alc;
        nex_blk->prev_footer = prev_blk->header ^ sf_magic();


        //remove blk from free list
        sf_block* prev_blk = blk->body.links.prev;
        sf_block* next_blk = blk->body.links.next;
        blk->body.links.prev = NULL;
        blk->body.links.next = NULL;
        prev_blk->body.links.next = next_blk;
        next_blk->body.links.prev = prev_blk;

        //remove prev_blk from free list
        sf_block* prev_blk_prev = prev_blk->body.links.prev;
        sf_block* prev_blk_next = prev_blk->body.links.next;
        prev_blk->body.links.prev = NULL;
        prev_blk->body.links.next = NULL;
        prev_blk_prev->body.links.next = prev_blk_next;
        prev_blk_next->body.links.prev = prev_blk_prev;

        //add prev_blk to FRONT of free list
        sf_block* sentinal = find_smallest_list(prev_blk);
        sf_block* sentinal_next = sentinal->body.links.next;
        prev_blk->body.links.next = sentinal_next;
        prev_blk->body.links.prev = sentinal;
        sentinal_next->body.links.prev = prev_blk;
        sentinal->body.links.next = prev_blk;
    }

    //if next block is free and prev block is coalesced, then coalesce next block as well
    if(bool_prv_coalesced == 1 && ((nex_blk->header & 0x2) == 0)){
        prev_blk->header += (nex_blk->header & BLOCK_SIZE_MASK);
        nex_nex_blk->prev_footer = prev_blk->header ^ sf_magic();

        //remove nex_blk from free list
        sf_block* prev_blk = nex_blk->body.links.prev;
        sf_block* next_blk = nex_blk->body.links.next;
        nex_blk->body.links.prev = NULL;
        nex_blk->body.links.next = NULL;
        prev_blk->body.links.next = next_blk;
        next_blk->body.links.prev = prev_blk;

        //remove prev_blk from free list
        sf_block* prev_blk_prev = prev_blk->body.links.prev;
        sf_block* prev_blk_next = prev_blk->body.links.next;
        prev_blk->body.links.prev = NULL;
        prev_blk->body.links.next = NULL;
        prev_blk_prev->body.links.next = prev_blk_next;
        prev_blk_next->body.links.prev = prev_blk_prev;

        //add prev_blk to FRONT of list
        sf_block* sentinal = find_smallest_list(prev_blk);
        sf_block* sentinal_next = sentinal->body.links.next;
        prev_blk->body.links.next = sentinal_next;
        prev_blk->body.links.prev = sentinal;
        sentinal_next->body.links.prev = prev_blk;
        sentinal->body.links.next = prev_blk;
    }

    //if next block is free and prev block cannot be coalesced, then coalesce next block only
    if(bool_prv_coalesced == 0 && ((nex_blk->header & 0x2) == 0)){
        blk->header += (nex_blk->header & BLOCK_SIZE_MASK);
        nex_nex_blk->prev_footer = blk->header ^ sf_magic();

        //remove nex_blk from free list
        sf_block* prev_blk = nex_blk->body.links.prev;
        sf_block* next_blk = nex_blk->body.links.next;
        nex_blk->body.links.prev = NULL;
        nex_blk->body.links.next = NULL;
        prev_blk->body.links.next = next_blk;
        next_blk->body.links.prev = prev_blk;

        //remove blk from free list
        sf_block* prev_blk2 = blk->body.links.prev;
        sf_block* next_blk2 = blk->body.links.next;
        blk->body.links.prev = NULL;
        blk->body.links.next = NULL;
        prev_blk2->body.links.next = next_blk2;
        next_blk2->body.links.prev = prev_blk2;

        //add blk to FRONT of list
        sf_block* sentinal = find_smallest_list(blk);
        sf_block* sentinal_next = sentinal->body.links.next;
        blk->body.links.next = sentinal_next;
        blk->body.links.prev = sentinal;
        sentinal_next->body.links.prev = blk;
        sentinal->body.links.next = blk;
    }

    return ;
}

void *sf_realloc(void *pp, size_t rsize) {
    sf_block* blk = (sf_block*)pp;
    void* prev_loctn = pp - ((*(sf_footer*)(pp) ^ sf_magic()) & BLOCK_SIZE_MASK);
    void* nex_loctn = pp + (blk->header & BLOCK_SIZE_MASK);
    sf_block* prev_blk = (sf_block*)prev_loctn;
    sf_block* nex_blk = (sf_block*)nex_loctn;
    void* mal_ptr;
    int payload_sz = (blk->header & BLOCK_SIZE_MASK) -16;

    //error handling
    if(pp == NULL || ((blk->header & 0x2) == 0) || (void *)&blk->header < sf_mem_start() ||
        (void *)&blk->prev_footer > sf_mem_end() || (blk->header & BLOCK_SIZE_MASK) < 32 ||
        (((blk->header & 0x1) == 0) && ((prev_blk->header & 0x2) != 0)) || (blk->header != (nex_blk->prev_footer ^ sf_magic())))
        sf_errno = EINVAL;
    if(rsize ==0){
        sf_free(pp);
        return NULL;
    }

    //reallocating to larger size
    if(rsize > (blk->header & BLOCK_SIZE_MASK) - 16){
        if((mal_ptr = sf_malloc(rsize)) == NULL)
            return NULL;

        sf_block* memcpy_blk = (sf_block*)mal_ptr;
        sf_block* after_memcpy_blk = mal_ptr + (memcpy_blk->header & BLOCK_SIZE_MASK);//CREATE FOOTER INSTEAD

        // memcpy(memcpy_blk, blk, rsize);
        memcpy_blk = blk;

        sf_free(pp);

        memcpy_blk->header = memcpy_blk->header | 0x2;
        after_memcpy_blk->prev_footer = memcpy_blk->header ^ sf_magic();//CREATE FOOTER INSTEAD

        return mal_ptr + 2*sizeof(sf_header);
    }

    //reallocating to smaller size
    if(rsize < (blk->header & BLOCK_SIZE_MASK)){
        //no splitting case
        if(payload_sz - rsize <32){
            return pp + 2*sizeof(sf_header);
        }
        //splitting case
        else{
            int split_amt = rsize + 16;
            int splitter_blk_sz = ((blk->header & BLOCK_SIZE_MASK) - split_amt);
            int bool_prv_alc = (blk->header & BLOCK_SIZE_MASK) & 0x1;
            blk->header = split_amt + bool_prv_alc + 2;

            //create splitter block
            sf_block splt_blk = {};
            sf_block* splt_blk_ptr = &splt_blk;
            splt_blk_ptr = (struct sf_block*)(pp + split_amt);
            splt_blk_ptr->prev_footer = blk->header ^ sf_magic();
            //splt_blk should be free but set it to allocated in order to call sf_free() for coalescing
            splt_blk_ptr->header = (splitter_blk_sz | 0x3);
            nex_blk->prev_footer = splt_blk_ptr->header & sf_magic();

            //coalescing
            sf_free(splt_blk_ptr);

            //place in a free list
            sf_block* sentinal = find_smallest_list(splt_blk_ptr);
            sf_block* sentinal_prev = sentinal->body.links.prev;
            splt_blk_ptr->body.links.next = sentinal;
            splt_blk_ptr->body.links.prev = sentinal_prev;
            sentinal->body.links.prev = splt_blk_ptr;
            sentinal_prev->body.links.next = splt_blk_ptr;

            return pp + 2*sizeof(sf_header);
        }
    }

    return NULL;
}
