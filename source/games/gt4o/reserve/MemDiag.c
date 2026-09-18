#include <stdbool.h>

#include "core/Target.h"
#include "core/game/IO.h"
#include "core/ps2/Memory.h"
#include "core/ps2/Sio.h"

#include "DevRam.h"
#include "HeapWatch.h"
#include "MemDiag.h"

/*
    Memory diagnostics for dev RAM mode: pcsx2 builds, PCSX2's 128 MB option on.

    This lives in the dev RAM reserve, so it only ever runs with 128 MB. There
    the game has four times the heap it ships with, which hides exactly the
    problems a PS2 would have. So this plays the stock heap alongside the real
    one:

      stock heap   DevRam.c starts the pool at the shipped base's offset within
                   1 MB, so every block lands where it would on a PS2, with the
                   same alignment padding - and nothing here changes the size
                   of a heap block, so that holds at every moment. Each new
                   block is checked as it is handed out: if it ends past the
                   shipped 23 MB, a PS2 could not have placed it anywhere -
                   that allocation would be the blue clock. The closest any
                   allocation came is kept per race.
      free memory  the game's "largest free block" and "total free" answers are
                   what that stock heap would give - its holes below the
                   frontier, and what is left of it past the frontier. Code that
                   sizes itself to free memory (0x18B8DC takes all but 200 KB of
                   the largest block, up to 13 MB) then takes what it would on a
                   PS2; left alone it would take far more here.
      holders      every live block is filed under the two callers found on the
                   stack past the heap's own code and operator new, so the
                   console can list who holds the heap at the worst moment -
                   the way to compare one run with another.
      hooks        blocks with plugin code on the stack are also charged to the
                   hooks, so the console can say what the mod itself holds.
      course       the heap still gives a race its 0xBC8000-byte course block,
                   but the course arena - where the course loader and photo
                   mode take the block from - is moved onto the 24 MB course
                   area past the pool (DevRam.h), so a bigger track still
                   loads. The part a PS2 would not have is filled with SENTINEL
                   each time the block is handed out, and anything the game
                   writes there is noticed. When the race frees the course
                   block, the heap's own block is what goes back. (An earlier
                   version made the heap block itself 24 MB. Blocks placed
                   above it during a race then outlived it 12 MB higher than on
                   a PS2, and every check after the race read them as far over.)

    When an allocation would not fit a PS2, or course data runs past the stock
    block, it says so on the console and the loading clock spins yellow instead
    of grey until the next race load. The game carries on - with 128 MB there
    is room. The addresses are in the target header, under "memory
    diagnostics".
*/

/* The course block in dev RAM mode: 24 MB, against 0xBC8000 (12,064 KB), in
   the course area past the pool, with the game's 0x100 bytes of slack. */
#define COURSE_BLOCK_DEV   DEVRAM_COURSE_BLOCK
#define COURSE_SLACK       DEVRAM_COURSE_SLACK

/* The pool as POOL_SETUP_FUNC builds it: base aligned up to 16, size rounded
   down to 16. */
#define POOL_SIZE_OF(base, top) ((((top) - (((base) + 15) & ~15))) & ~15)
#define POOL_SIZE_STOCK    ((int)POOL_SIZE_OF(POOL_BASE_VALUE, MEMORY_SIZE_VALUE))

/* The pool object's fields - see the target header. */
#define POOL_END      (*(volatile unsigned int*)(POOL_OBJECT + 0x4))
#define POOL_START    (*(volatile unsigned int*)(POOL_OBJECT + 0x8))
#define POOL_BINS     ((volatile unsigned int*)(POOL_OBJECT + 0xC))
#define POOL_LOCK     ((void*)(POOL_OBJECT + 0x4C))

/* A console note once less than this is left at the top of the stock heap. */
#define TIGHT_BYTES        (64 * 1024)
/* Keep a list of the heap's holders when an allocation comes this close. */
#define SNAPSHOT_BELOW     (256 * 1024)

/* What fills the course block beyond 0xBC8000 until the game writes there. */
#define SENTINEL           0xA5E7C0DEu

/* Accounting. The tables sit in the reserve's own free space, past _devram_end:
   one slot per live block (a hash on its address), one entry per pair of
   callers (found by HeapWatch_FindCallers), one per plugin call site. */
#define LIVE_BITS          15
#define LIVE_SLOTS         (1 << LIVE_BITS)
#define LIVE_LIMIT         (LIVE_SLOTS * 3 / 4)
#define HOLDER_BITS        12
#define HOLDER_MAX         2048
#define HOOK_MAX           48
#define TOP_N              10

typedef void  (*LockFn)(void* lock);
typedef void  (*CourseAllocFn)(unsigned int size);
typedef void* (*ArenaTakeFn)(void* arena, int unused);
typedef void* (*ArenaInitFn)(void* arena, void* base, unsigned int total, unsigned int blockSize);
typedef void* (*ArenaReleaseFn)(void* arena);
typedef int   (*Pass4Fn)(int a0, int a1, int a2, int a3);

typedef struct
{
    int realTotal;     /* what the game's queries answer here */
    int realLargest;
    int stockTotal;    /* what they would answer on a PS2 */
    int stockLargest;
    int stockTop;      /* left in the stock heap's top block; below 0, over */
    int holes;         /* free below the frontier, stock side, less 16 each */
} HeapView;

typedef struct { unsigned int block, size; unsigned short holder, hook; } Live;
typedef struct { unsigned int ra1, ra2; int live, blocks; } Holder;
typedef struct { unsigned int site; int live, peak, blocks; } HookSite;

#define MD_PRINT(...)                   \
    do {                                \
        char _md_buf[224];              \
        _sprintf(_md_buf, __VA_ARGS__); \
        Sio_Puts(_md_buf);              \
    } while (0)

#define MD_STR2(x) #x
#define MD_STR(x)  MD_STR2(x)

extern unsigned int _devram_end[];

/* Yellow, beside the clock's own grey, red and blue. */
static const float s_whistle[4] = { 0.9f, 0.8f, 0.0f, 1.0f };

/* course */
static void*         s_coursePoolBlock;  /* the heap's course block, while the arena is on the course area */
static unsigned int* s_courseBlock;      /* handed out, its extra room filled */
static bool          s_overCourse;

/* heap state and events */
static bool          s_overHeap;         /* the frontier is past the stock end now */
static bool          s_tight;
static bool          s_overEvent;        /* an allocation this race would not fit a PS2 */
static bool          s_overPrinted;
static int           s_overBy, s_overSize, s_overHooks;
static unsigned int  s_overRa1, s_overRa2;
static int           s_closest;          /* least room any allocation left, this race */
static unsigned int  s_calls;

/* the heap's holders at the worst moment */
static Holder        s_top[TOP_N];
static int           s_nTop;
static int           s_topLeft;
static int           s_topHooks;

/* tables */
static Live*           s_live;           /* 0 until installed */
static Holder*         s_holders;        /* [0] takes whatever does not fit */
static unsigned short* s_holderIndex;
static HookSite*       s_hookSites;
static int             s_nLive, s_nHolders, s_nHookSites;
static bool            s_liveFull;
static int             s_hookLive, s_hookMost;

static bool WordIs(unsigned int addr, unsigned int value)
{
    return *(volatile unsigned int*)addr == value;
}

/* ---- the stock heap ------------------------------------------------------------ */

/* Where an address would be in the stock heap, as an offset from its start.
   The pool is laid out exactly as the stock one, so that is where it is. */
static int StockOffset(unsigned int addr, unsigned int start)
{
    return (int)(addr - start);
}

static void __attribute__((optimize("O2"))) ViewHeap(HeapView* v)
{
    unsigned int start = POOL_START, end = POOL_END, frontier = end;
    unsigned int realMax = 0;
    int realSum = 0, holes = 0, holeMax = 0, top, big, i;

    ((LockFn)ADDR_Pool_Lock)(POOL_LOCK);
    for (i = 0; i < 16; i++)
    {
        unsigned int b = POOL_BINS[i];

        while (b != 0)
        {
            unsigned int be = ((volatile unsigned int*)b)[1];
            unsigned int size = be - b;

            realSum += (int)size - 16;
            if (size > realMax)
                realMax = size;

            if (be == end)
                frontier = b;          /* the top block */
            else
            {
                /* A hole: what of it lies inside the stock heap. */
                int ob = StockOffset(b, start);
                int oe = StockOffset(be, start);

                if (ob < POOL_SIZE_STOCK)
                {
                    int eff = (oe > POOL_SIZE_STOCK ? POOL_SIZE_STOCK : oe) - ob;

                    if (eff > 16)
                        holes += eff - 16;
                    if (eff > holeMax)
                        holeMax = eff;
                }
            }
            b = ((volatile unsigned int*)b)[3];
        }
    }
    ((LockFn)ADDR_Pool_Unlock)(POOL_LOCK);

    v->realTotal = realSum;
    v->realLargest = realMax > 16 ? (int)realMax - 16 : 0;
    v->stockTop = POOL_SIZE_STOCK - StockOffset(frontier, start);
    v->holes = holes;
    top = v->stockTop > 0 ? v->stockTop : 0;
    v->stockTotal = holes + (top > 16 ? top - 16 : 0);
    big = holeMax > top ? holeMax : top;
    v->stockLargest = big > 16 ? big - 16 : 0;
}

/* GetMaxMemorySize and its total-free twin, answered as the stock heap would. */
int MemDiag_LargestFree(void)
{
    HeapView v;

    ViewHeap(&v);
    return v.realLargest < v.stockLargest ? v.realLargest : v.stockLargest;
}

int MemDiag_TotalFree(void)
{
    HeapView v;

    ViewHeap(&v);
    return v.realTotal < v.stockTotal ? v.realTotal : v.stockTotal;
}

/* ---- who holds the heap -------------------------------------------------------- */

/* Callers come from HeapWatch_FindCallers, whose plugin ranges leave the
   reserve out: it only exists with 128 MB, and what it calls through to (the
   course block) is the game's own allocation. */

static int __attribute__((optimize("O2"))) HolderFor(unsigned int ra1, unsigned int ra2)
{
    unsigned int i = ((ra1 ^ (ra2 * 0x9E3779B1u)) * 2654435761u) >> (32 - HOLDER_BITS);

    for (;;)
    {
        unsigned int k = s_holderIndex[i];

        if (k == 0)
        {
            if (s_nHolders >= HOLDER_MAX)
                return 0;
            k = (unsigned int)s_nHolders++;
            s_holders[k].ra1 = ra1;
            s_holders[k].ra2 = ra2;
            s_holderIndex[i] = (unsigned short)k;
            return (int)k;
        }
        if (s_holders[k].ra1 == ra1 && s_holders[k].ra2 == ra2)
            return (int)k;
        i = (i + 1) & ((1u << HOLDER_BITS) - 1);
    }
}

static int HookSiteFor(unsigned int site)
{
    int i;

    for (i = 0; i < s_nHookSites && s_hookSites[i].site != site; i++)
        ;
    if (i == s_nHookSites)
    {
        if (s_nHookSites < HOOK_MAX)
            s_hookSites[s_nHookSites++].site = site;
        else
            i = HOOK_MAX - 1;          /* the last entry takes the overflow */
    }
    return i;
}

static unsigned int LiveHome(unsigned int block)
{
    return ((block >> 4) * 2654435761u) >> (32 - LIVE_BITS);
}

/* Takes a block's entry out of the accounting. */
static void Forget(const Live* e)
{
    s_holders[e->holder].live -= (int)e->size;
    s_holders[e->holder].blocks--;
    if (e->hook != 0)
    {
        HookSite* h = &s_hookSites[e->hook - 1];

        h->live -= (int)e->size;
        h->blocks--;
        s_hookLive -= (int)e->size;
    }
}

static void __attribute__((optimize("O2"))) LiveAdd(unsigned int block, unsigned int size,
                                                     int holder, int hook)
{
    unsigned int i = LiveHome(block);

    while (s_live[i].block != 0)
    {
        if (s_live[i].block == block)
        {
            /* A stale entry for the same address (its free went unseen). */
            Forget(&s_live[i]);
            s_nLive--;
            break;
        }
        i = (i + 1) & (LIVE_SLOTS - 1);
    }
    if (s_live[i].block == 0 && s_nLive >= LIVE_LIMIT)
    {
        s_liveFull = true;
        return;
    }
    s_live[i].block = block;
    s_live[i].size = size;
    s_live[i].holder = (unsigned short)holder;
    s_live[i].hook = (unsigned short)(hook + 1);
    s_nLive++;
}

/* Finds and removes a block's entry, closing the gap behind it. */
static bool __attribute__((optimize("O2"))) LiveTake(unsigned int block, Live* out)
{
    unsigned int i = LiveHome(block), j, k;

    while (s_live[i].block != block)
    {
        if (s_live[i].block == 0)
            return false;
        i = (i + 1) & (LIVE_SLOTS - 1);
    }
    *out = s_live[i];

    j = i;
    for (;;)
    {
        j = (j + 1) & (LIVE_SLOTS - 1);
        if (s_live[j].block == 0)
            break;
        k = LiveHome(s_live[j].block);
        if (i <= j ? (k <= i || k > j) : (k <= i && k > j))
        {
            s_live[i] = s_live[j];
            i = j;
        }
    }
    s_live[i].block = 0;
    s_nLive--;
    return true;
}

/* The TOP_N holders holding the most, into s_top. */
static void __attribute__((optimize("O2"))) TakeSnapshot(int left)
{
    int k, i, best, n;
    int taken[TOP_N];

    n = 0;
    for (k = 0; k < TOP_N; k++)
    {
        best = -1;
        for (i = 0; i < s_nHolders; i++)
        {
            int t;
            bool used = false;

            if (s_holders[i].live <= 0)
                continue;
            for (t = 0; t < n; t++)
                if (taken[t] == i)
                    used = true;
            if (used)
                continue;
            if (best < 0 || s_holders[i].live > s_holders[best].live)
                best = i;
        }
        if (best < 0)
            break;
        taken[n] = best;
        s_top[n++] = s_holders[best];
    }
    s_nTop = n;
    s_topLeft = left;
    s_topHooks = s_hookLive;
}

/* Would this block have fit the stock heap? */
static void StockCheck(unsigned int block, unsigned int size, const HeapCallers* c)
{
    int left = POOL_SIZE_STOCK - (StockOffset(block, POOL_START) + (int)size);

    if (left < s_closest)
    {
        s_closest = left;
        if (left < SNAPSHOT_BELOW && (s_topLeft > left + 1024 || (left < 0 && s_topLeft >= 0)))
            TakeSnapshot(left);
    }
    if (left < 0 && !s_overEvent)
    {
        s_overEvent = true;
        s_overPrinted = false;
        s_overBy = -left;
        s_overSize = (int)size;
        s_overRa1 = c->ra1;
        s_overRa2 = c->ra2;
        s_overHooks = s_hookLive;
    }
}

/* From MemDiag_AllocSeen, with the pool locked. */
void MemDiag_OnAlloc(unsigned int block, unsigned int pool, const unsigned int* sp)
{
    HeapCallers c;
    unsigned int size;
    int holder, hook = -1;

    if (pool != POOL_OBJECT || block == 0 || s_live == 0)
        return;
    size = ((unsigned int*)block)[1] - block;
    HeapWatch_FindCallers(sp, &c);

    holder = HolderFor(c.ra1, c.ra2);
    s_holders[holder].live += (int)size;
    s_holders[holder].blocks++;

    if (c.plugin != 0)
    {
        HookSite* h;

        hook = HookSiteFor(c.plugin);
        h = &s_hookSites[hook];
        h->live += (int)size;
        h->blocks++;
        if (h->live > h->peak)
            h->peak = h->live;
        s_hookLive += (int)size;
        if (s_hookLive > s_hookMost)
            s_hookMost = s_hookLive;
    }

    LiveAdd(block, size, holder, hook);
    StockCheck(block, size, &c);
}

/* From MemDiag_FreeSeen, with the pool locked. */
void MemDiag_OnFree(unsigned int block, unsigned int pool)
{
    Live e;

    if (pool != POOL_OBJECT || s_live == 0 || s_nLive == 0)
        return;
    if (LiveTake(block, &e))
        Forget(&e);
}

/*
    The two stand-ins, called in place of the pool routines' lock calls:

    MemDiag_AllocSeen   instead of malloc's unlock at 0x5275BC: a0 is the lock,
                        s4 the new block (0 if none), s6 the pool, sp the pool
                        routine's frame. Notes the block, then unlocks.
    MemDiag_FreeSeen    instead of free's lock at 0x5277C4: a0 is the lock, s1
                        the block, s2 the pool. Locks, then notes the free.

    Both leave the pool routine's registers as its own call would.
*/
void MemDiag_AllocSeen(void);
void MemDiag_FreeSeen(void);

__asm__(
    ".pushsection .text\n"
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".align 2\n"
    ".globl MemDiag_AllocSeen\n"
    ".type MemDiag_AllocSeen, @function\n"
    "MemDiag_AllocSeen:\n"
    "    addiu $sp, $sp, -32\n"
    "    sd    $ra, 0($sp)\n"
    "    sd    $a0, 8($sp)\n"
    "    move  $a0, $s4\n"
    "    move  $a1, $s6\n"
    "    jal   MemDiag_OnAlloc\n"
    "    addiu $a2, $sp, 32\n"
    "    ld    $a0, 8($sp)\n"
    "    ld    $ra, 0($sp)\n"
    "    li    $t9, " MD_STR(ADDR_Pool_Unlock) "\n"
    "    jr    $t9\n"
    "    addiu $sp, $sp, 32\n"
    ".size MemDiag_AllocSeen, .-MemDiag_AllocSeen\n"
    ".globl MemDiag_FreeSeen\n"
    ".type MemDiag_FreeSeen, @function\n"
    "MemDiag_FreeSeen:\n"
    "    addiu $sp, $sp, -32\n"
    "    sd    $ra, 0($sp)\n"
    "    li    $t9, " MD_STR(ADDR_Pool_Lock) "\n"
    "    jalr  $t9\n"
    "    nop\n"
    "    move  $a0, $s1\n"
    "    jal   MemDiag_OnFree\n"
    "    move  $a1, $s2\n"
    "    ld    $ra, 0($sp)\n"
    "    jr    $ra\n"
    "    addiu $sp, $sp, 32\n"
    ".size MemDiag_FreeSeen, .-MemDiag_FreeSeen\n"
    ".set pop\n"
    ".popsection\n");

/* ---- printing -------------------------------------------------------------------- */

static void PrintTop(void)
{
    int i;

    MD_PRINT("[gt4hooks] mem:   at that moment %d KB was left; the hooks held %d KB;"
             " the biggest holders (caller <- its caller):\n",
             s_topLeft / 1024, s_topHooks / 1024);
    for (i = 0; i < s_nTop; i++)
        MD_PRINT("[gt4hooks] mem:     0x%08X <- 0x%08X  %d KB in %d blocks\n",
                 s_top[i].ra1, s_top[i].ra2, s_top[i].live / 1024, s_top[i].blocks);
}

/* The plugin call sites holding the most, biggest first. */
static void PrintHookSites(void)
{
    int shown[4];
    int k, i, best;

    for (k = 0; k < 4; k++)
    {
        best = -1;
        for (i = 0; i < s_nHookSites; i++)
        {
            if (s_hookSites[i].live <= 0 || (k > 0 && i == shown[0]) ||
                (k > 1 && i == shown[1]) || (k > 2 && i == shown[2]))
                continue;
            if (best < 0 || s_hookSites[i].live > s_hookSites[best].live)
                best = i;
        }
        if (best < 0)
            break;
        shown[k] = best;
        MD_PRINT("[gt4hooks] mem:   hook site 0x%08X%s: %d KB in %d blocks\n",
                 s_hookSites[best].site,
                 s_hookSites[best].site - CAVE2_ADDRESS < CAVE2_RESERVE ? " (RaceHud/RaceAspect)" : "",
                 s_hookSites[best].live / 1024, s_hookSites[best].blocks);
    }
}

/* How much of the heap the disc's table of contents holds, against the retail
   disc's (HeapWatch.c watches it). `left` is how far short of the end of the
   stock heap an allocation came - below 0, past it - and the line says where
   it would have been with the retail disc's table instead. Nothing when the
   two tables are within half a KB. */
static void PrintToc(int left)
{
    const HeapToc* t = HeapWatch_Toc();
    int more = (int)t->bytes - TOC_RETAIL_BYTES;
    int alt = left + more;

    if (t->bytes == 0 || (more < 512 && more > -512))
        return;
    MD_PRINT("[gt4hooks] mem:   of that heap, GT4.VOL's table of contents holds %d KB for %d files;"
             " with the retail disc's (%d KB, %d files) it would be %d KB %s\n",
             ((int)t->bytes + 512) / 1024, (int)t->files, (TOC_RETAIL_BYTES + 512) / 1024,
             TOC_RETAIL_FILES, (alt < 0 ? -alt : alt) / 1024,
             alt < 0 ? "past the end" : "short of the end");
}

/* ---- checks ---------------------------------------------------------------------- */

static void CheckHeap(void)
{
    HeapView v;
    bool over, tight;

    if (s_overEvent && !s_overPrinted)
    {
        s_overPrinted = true;
        MD_PRINT("[gt4hooks] mem: OVER the stock heap - a PS2 could not have placed a %d KB"
                 " block asked for at 0x%08X <- 0x%08X (%d KB short): the blue clock."
                 " The hooks held %d KB\n",
                 (s_overSize + 1023) / 1024, s_overRa1, s_overRa2, (s_overBy + 1023) / 1024,
                 s_overHooks / 1024);
        PrintToc(-(int)s_overBy);
        PrintTop();
        PrintHookSites();
    }

    ViewHeap(&v);
    over = v.stockTop < 0;
    tight = !over && v.stockTop < TIGHT_BYTES;
    if (over == s_overHeap && tight == s_tight)
        return;
    s_overHeap = over;
    s_tight = tight;

    if (over)
        MD_PRINT("[gt4hooks] mem: the heap is past the end of the stock heap by %d KB\n",
                 (-v.stockTop) / 1024);
    else if (tight)
        MD_PRINT("[gt4hooks] mem: tight - %d KB left at the top of the stock heap,"
                 " %d KB more in holes\n", v.stockTop / 1024, v.holes / 1024);
    else
        MD_PRINT("[gt4hooks] mem: back within the stock heap - %d KB left at the top\n",
                 v.stockTop / 1024);
}

static void __attribute__((optimize("O2"))) Fill(unsigned int* p, unsigned int* end)
{
    while (p < end)
        *p++ = SENTINEL;
}

/* The offset, from `from` on, of the first 1 KB the course data never reached. */
static unsigned int __attribute__((optimize("O2"))) Reach(const unsigned int* block,
                                                          unsigned int from, unsigned int size)
{
    unsigned int off, i;

    for (off = from; off + 1024 <= size; off += 1024)
    {
        const unsigned int* w = block + off / 4;

        for (i = 0; i < 256 && w[i] == SENTINEL; i++)
            ;
        if (i == 256)
            return off;
    }
    return size;
}

static void CheckCourse(void)
{
    const volatile unsigned int* arena = (const volatile unsigned int*)COURSE_ARENA;
    const unsigned int* block = s_courseBlock;
    unsigned int i, reach;

    if (block == 0 || s_overCourse)
        return;
    /* Only while that block is allocated and handed out. */
    if (*(volatile int*)COURSE_BLOCK_LIVE == 0 || arena[4] == 0)
        return;

    for (i = 0; i < 16; i++)
        if (block[COURSE_BLOCK_STOCK / 4 + i] != SENTINEL)
            break;
    if (i == 16)
        return;

    reach = Reach(block, COURSE_BLOCK_STOCK, arena[2]);
    s_overCourse = true;
    MD_PRINT("[gt4hooks] mem: OVER the stock course block - the data reaches %d KB,"
             " a PS2 has %d KB (over by %d KB)\n",
             reach / 1024, COURSE_BLOCK_STOCK / 1024, (reach - COURSE_BLOCK_STOCK) / 1024);
}

/* ---- the course block --------------------------------------------------------------- */

/* Stands in for 0x276388 at both of its calls. The heap gives the block its
   stock size as ever; then, for a stock-size block, the arena is moved onto
   the course area - 0x2DF580 empties it first and returns the heap's block,
   which is kept for MemDiag_CourseRelease. Anything else, or a block that is
   already there, goes through unchanged. */
void MemDiag_CourseAlloc(unsigned int size)
{
    bool wasLive = *(volatile int*)COURSE_BLOCK_LIVE != 0;

    ((CourseAllocFn)ADDR_CourseBlock_Alloc)(size);
    if (!wasLive && *(volatile int*)COURSE_BLOCK_LIVE && size == COURSE_BLOCK_STOCK &&
        s_coursePoolBlock == 0)
        s_coursePoolBlock = ((ArenaInitFn)ADDR_Arena_Init)((void*)COURSE_ARENA,
                                                           (void*)DEVRAM_COURSE_AREA,
                                                           COURSE_BLOCK_DEV + COURSE_SLACK,
                                                           COURSE_BLOCK_DEV);
}

/* Stands in for the arena release in the course block's free (0x276418), whose
   answer the game then frees: while the arena is on the course area, that is
   the heap's own block. */
void* MemDiag_CourseRelease(void* arena)
{
    void* base = ((ArenaReleaseFn)ADDR_Arena_Release)(arena);

    if (s_coursePoolBlock != 0 && base == (void*)DEVRAM_COURSE_AREA)
    {
        base = s_coursePoolBlock;
        s_coursePoolBlock = 0;
        s_courseBlock = 0;
    }
    return base;
}

/* Stands in for the arena's take at the course loader and photo mode: hands the
   block out as before, with everything past 0xBC8000 filled with SENTINEL. */
void* MemDiag_CourseTake(void* arena, int unused)
{
    unsigned int* block = ((ArenaTakeFn)ADDR_Arena_Take)(arena, unused);
    unsigned int size = ((volatile unsigned int*)arena)[2];

    s_courseBlock = 0;
    s_overCourse = false;
    if (block != 0 && size > COURSE_BLOCK_STOCK)
    {
        Fill(block + COURSE_BLOCK_STOCK / 4, block + size / 4);
        s_courseBlock = block;
    }
    return block;
}

/* Stands in for the last call AllocateRaceBuffer makes, once the course, car
   and work buffers are all in: how close the last race came, then a fresh
   count for this one. */
int MemDiag_AfterRaceBuffers(int a0, int a1, int a2, int a3)
{
    int r = ((Pass4Fn)ADDR_RaceBuffers_LastTarget)(a0, a1, a2, a3);
    HeapView v;

    CheckHeap();                       /* anything still unprinted */
    if (s_closest != 0x7FFFFFFF)
    {
        MD_PRINT("[gt4hooks] mem: since the last race load, the closest any allocation came to"
                 " the end of the stock heap: %d KB %s\n",
                 (s_closest < 0 ? -s_closest : s_closest) / 1024,
                 s_closest < 0 ? "PAST it" : "short of it");
        PrintToc(s_closest);
        if (s_nTop > 0)
            PrintTop();
    }

    ViewHeap(&v);
    MD_PRINT("[gt4hooks] mem: race buffers in - course block %d KB (PS2 %d KB); stock heap"
             " %d KB left at the top, %d KB in holes; the hooks hold %d KB in %d blocks%s\n",
             (int)((volatile unsigned int*)COURSE_ARENA)[2] / 1024, COURSE_BLOCK_STOCK / 1024,
             v.stockTop / 1024, v.holes / 1024, s_hookLive / 1024, s_nLive,
             s_liveFull ? " (block table full - figures are low)" : "");

    s_closest = 0x7FFFFFFF;
    s_topLeft = 0x7FFFFFFF;
    s_nTop = 0;
    s_overEvent = false;
    return r;
}

/* ---- the loading clock -------------------------------------------------------------- */

/* Stands in for LoadingConfigPS2::getColor: the game's red and blue first, then
   yellow when over budget, then the usual grey. The checks ride on it because
   it runs every frame a loading screen is up; every fourth is plenty. */
void* MemDiag_ClockColor(void* out, const char* config)
{
    const unsigned char* src;
    unsigned char* dst = (unsigned char*)out;
    int i;

    if ((s_calls++ & 3) == 0)
    {
        CheckHeap();
        CheckCourse();
    }

    if (*(const volatile int*)(config + 8))
        src = (const unsigned char*)(config + 0x20);
    else if (*(const volatile int*)(config + 0xC))
        src = (const unsigned char*)(config + 0x30);
    else if (s_overHeap || s_overEvent || s_overCourse)
        src = (const unsigned char*)s_whistle;
    else
        src = (const unsigned char*)(config + 0x10);

    /* The game copies this unaligned, so bytewise here too. */
    for (i = 0; i < 16; i++)
        dst[i] = src[i];
    return out;
}

/* ---- install -------------------------------------------------------------------------- */

void MemDiag_Install(void)
{
    bool clock, answers, course, heap;
    unsigned int tables = ((unsigned int)_devram_end + 63) & ~63u;
    unsigned int live = tables;
    unsigned int holders = live + LIVE_SLOTS * sizeof(Live);
    unsigned int index = holders + HOLDER_MAX * sizeof(Holder);
    unsigned int hookSites = index + (1u << HOLDER_BITS) * sizeof(unsigned short);
    unsigned int tablesEnd = hookSites + HOOK_MAX * sizeof(HookSite);
    unsigned int* p;

    s_closest = 0x7FFFFFFF;
    s_topLeft = 0x7FFFFFFF;

    clock = WordIs(ADDR_LoadingClock_GetColor,     INSN_LoadingClock_GetColor0) &&
            WordIs(ADDR_LoadingClock_GetColor + 4, INSN_LoadingClock_GetColor1);
    if (clock)
        HOOK(ADDR_LoadingClock_GetColor, &MemDiag_ClockColor);

    answers = WordIs(ADDR_GetMaxMemorySize,      INSN_HeapQuery_0) &&
              WordIs(ADDR_GetMaxMemorySize + 4,  INSN_HeapQuery_1) &&
              WordIs(ADDR_GetFreeMemorySize,     INSN_HeapQuery_0) &&
              WordIs(ADDR_GetFreeMemorySize + 4, INSN_HeapQuery_1);
    if (answers)
    {
        HOOK(ADDR_GetMaxMemorySize,  &MemDiag_LargestFree);
        HOOK(ADDR_GetFreeMemorySize, &MemDiag_TotalFree);
    }

    course = WordIs(ADDR_CourseBlock_AllocRace,  INSN_JAL_CourseBlock_Alloc) &&
             WordIs(ADDR_CourseBlock_AllocAgain, INSN_JAL_CourseBlock_Alloc) &&
             WordIs(ADDR_CourseArena_TakeLoad,   INSN_JAL_Arena_Take) &&
             WordIs(ADDR_CourseArena_TakeBorrow, INSN_J_Arena_Take) &&
             WordIs(ADDR_CourseBlock_FreeRelease, INSN_JAL_Arena_Release) &&
             WordIs(ADDR_RaceBuffers_LastCall,   INSN_JAL_RaceBuffers_Last);
    if (course)
    {
        MAKE_JAL(ADDR_CourseBlock_FreeRelease, &MemDiag_CourseRelease);
        MAKE_JAL(ADDR_CourseBlock_AllocRace,  &MemDiag_CourseAlloc);
        MAKE_JAL(ADDR_CourseBlock_AllocAgain, &MemDiag_CourseAlloc);
        MAKE_JAL(ADDR_CourseArena_TakeLoad,   &MemDiag_CourseTake);
        MAKE_JMP(ADDR_CourseArena_TakeBorrow, &MemDiag_CourseTake);
        MAKE_JAL(ADDR_RaceBuffers_LastCall,   &MemDiag_AfterRaceBuffers);
    }

    heap = tablesEnd <= DEVRAM_ADDRESS + DEVRAM_RESERVE &&
           WordIs(ADDR_PoolAlloc_UnlockCall,     INSN_JAL_Pool_Unlock) &&
           WordIs(ADDR_PoolAlloc_UnlockCall + 4, INSN_PoolAlloc_UnlockSlot) &&
           WordIs(ADDR_PoolFree_LockCall,        INSN_JAL_Pool_Lock) &&
           WordIs(ADDR_PoolFree_LockCall + 4,    INSN_PoolFree_LockSlot);
    if (heap)
    {
        for (p = (unsigned int*)tables; p < (unsigned int*)tablesEnd; p++)
            *p = 0;
        s_holders = (Holder*)holders;
        s_holderIndex = (unsigned short*)index;
        s_hookSites = (HookSite*)hookSites;
        s_nHolders = 1;                /* [0] takes the overflow */
        s_live = (Live*)live;          /* last: it switches the accounting on */
        MAKE_JAL(ADDR_PoolAlloc_UnlockCall, &MemDiag_AllocSeen);
        MAKE_JAL(ADDR_PoolFree_LockCall,    &MemDiag_FreeSeen);
    }

    MD_PRINT("[gt4hooks] mem: diagnostics - loading clock %s; free-memory answers %s;"
             " course block %s; allocation checks %s; the stock heap is %d KB\n",
             clock ? "yellow when over budget" : "LEFT ALONE",
             answers ? "as on a PS2" : "LEFT ALONE",
             course ? "24 MB, past the heap" : "stock",
             heap ? "on" : "off (the stock heap is only sampled)",
             POOL_SIZE_STOCK / 1024);
}
