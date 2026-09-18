#pragma once

/*
    Heap watch, in every build - see HeapWatch.c.
*/

/* Return addresses found on the stack above a heap routine's frame: the first
   two outside the heap's own code and operator new, and the first into plugin
   code (0 where there is none). The watch's own code does not count as plugin
   code. reserve/MemDiag.c files blocks by these too. */
typedef struct
{
    unsigned int ra1;
    unsigned int ra2;
    unsigned int plugin;
} HeapCallers;

void HeapWatch_FindCallers(const unsigned int* sp, HeapCallers* c);

/* GT4.VOL's table of contents, which the game keeps in the heap from boot to
   the end of the session: the block it was read into, its pages, and the
   files and folders its record pages list. All zero until the volume is
   mounted; files and folders stay zero if its pages could not be read. The
   retail disc's figures are TOC_RETAIL_* in the target header. */
typedef struct
{
    unsigned int bytes;
    unsigned int pages;
    unsigned int files;
    unsigned int folders;
} HeapToc;

const HeapToc* HeapWatch_Toc(void);

/* Call last in init(): dev RAM mode's diagnostics take the allocator site
   first when they are on, and this then leaves it to them. */
void HeapWatch_Install(void);
