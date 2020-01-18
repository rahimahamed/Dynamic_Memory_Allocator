#ifndef MOREFUNC_H
#define MOREFUNC_H

#include "sfmm.h"

extern int first_mem_grow;

int empty_list_checker(sf_block dummy_blk);

size_t max_mem_list_checker(sf_block dummy_blk);

struct sf_block* find_smallest_list(sf_block* blk);

#endif