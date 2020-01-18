#include <stdio.h>
#include "sfmm.h"

int main(int argc, char const *argv[]) {
    sf_mem_init();

    void *x = sf_malloc(sizeof(int) * 8);
    void *y = sf_realloc(x, sizeof(char));
    printf("%p\n", x);
    printf("%p\n", y);

    sf_mem_fini();

    return EXIT_SUCCESS;
}
