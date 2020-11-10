#include "subroutine.h"
#include "allocator.h"

int main()
{
    char *ptr = nnalloc(1024);
    do_something("it works.");
    free(ptr);
    return 0;
}
