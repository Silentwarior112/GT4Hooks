#include <stdbool.h>

#include "core/Target.h"
#include "core/game/IO.h"
#include "core/pdistd/FileDevice.h"
#include "core/ps2/Memory.h"
#include "core/ps2/Sio.h"

#include "HeapWatch.h"

/*
    Heap watch, in every build.

    What a console can only report over the SIO port, which PCSX2's console
    collects:

      table of contents   GT4.VOL's table of contents sits in the heap from boot
                          to the end of the session, and it grows with every
                          file packed into the volume. Once the mount has read
                          it, this counts its files and folders and says how
                          much heap it holds against the retail disc's; every
                          failure report repeats that. See below.
      failed allocations  malloc's pool routine (0x527478) drops its lock at
                          0x5275BC. When it found no block, this prints the size
                          and alignment asked for, four callers on the stack
                          past the heap's own code and operator new, the heap's
                          largest and total free at that moment, the table of
                          contents, and what the hooks hold. The blocking
                          wrappers retry every 2 ms - the blue clock - so a
                          request that keeps failing is printed once.
      what the hooks hold every block allocated with plugin code on the stack is
                          charged to the first plugin address found there, and
                          dropped when free's pool routine (0x527790) sees it
                          again at 0x5277C4. The watch's own code is not plugin
                          code here: linkfile gathers it into one run, since
                          every allocation it sees leaves its return addresses
                          on the stack for later scans to find. A line goes out
                          each time the total passes another 16 KB. The tables
                          use the dead .cave2b run as scratch RAM, past anything
                          linked there, so they cost neither heap nor IME cave.
      reads into nothing  in builds without HostFS, the file device's readStream
                          is wrapped to name any file the game reads into a
                          buffer below the executable - NULL, in practice - and
                          who asked. The read then goes ahead as before. HostFS
                          owns that vtable slot in pcsx2 builds.

    The allocator reports run with the pool locked, so their printing is
    Sio_Puts and nothing else. Dev RAM mode's diagnostics (reserve/MemDiag.c)
    take the same two allocator sites for their own accounting; they are
    installed first, and this then leaves both sites to them. The table of
    contents is watched either way, and MemDiag reports it too.
*/

#define SCAN_WORDS     256
#define PLUGIN_SCAN    160
#define CHAIN_MAX      4
/* A return address this close to the one before it is taken for an earlier
   call from the same function, left on the stack, and skipped. */
#define SAME_FUNCTION  0x400

#define TRACK_BITS     10
#define TRACK_SLOTS    (1 << TRACK_BITS)
#define TRACK_LIMIT    (TRACK_SLOTS * 3 / 4)
#define SITE_MAX       32
#define REPORT_STEP    (16 * 1024)

#define HW_STR2(x) #x
#define HW_STR(x)  HW_STR2(x)

/* Little-endian halfword, a byte at a time: nothing here is known aligned. */
#define U16(p) ((unsigned int)(p)[0] | ((unsigned int)(p)[1] << 8))

typedef int (*PoolQueryFn)(void* pool);
typedef int (*ReadStreamFn)(FileDevice* device, FileInternalStream* stream,
                            MemorySpace* memSpace, int expand);
typedef unsigned int (*SetTableFn)(void* volume, const unsigned char* table, unsigned int bytes);
typedef unsigned int (*CacheInitFn)(void* cache, unsigned int pageSize, void* buffer,
                                    unsigned int bytes);
typedef const unsigned char* (*GetPageFn)(void* volume, unsigned int index);
typedef void (*ReleasePageFn)(void* volume, unsigned int index);

typedef struct { unsigned int block, size, site; } Tracked;
typedef struct { unsigned int site; int live, blocks; } Site;

extern unsigned char _cave2b_end[];
extern const unsigned char _heapwatch_start[], _heapwatch_end[];
extern const unsigned char _startup_start[], _startup_end[];

static unsigned int s_lastNeeded;
static unsigned int s_lastRa1;

static Tracked* s_track;       /* 0 until installed */
static Site*    s_sites;
static int      s_nTrack, s_nSites, s_hookLive, s_hookMost, s_reported;
static bool     s_full;

static HeapToc      s_toc;
static void*        s_tocVolume;
static unsigned int s_tocPageSize;

#if !PLATFORM_PCSX2
static ReadStreamFn s_readStream;
#endif

static void PutHex(unsigned int v)
{
    static const char digits[] = "0123456789ABCDEF";
    char s[11];
    int i;

    s[0] = '0';
    s[1] = 'x';
    for (i = 0; i < 8; i++)
        s[2 + i] = digits[(v >> (28 - 4 * i)) & 15];
    s[10] = 0;
    Sio_Puts(s);
}

static void PutDec(unsigned int v)
{
    char s[11];
    int i = 10;

    s[10] = 0;
    do
    {
        s[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    Sio_Puts(s + i);
}

/* To the nearest KB. */
static void PutKB(unsigned int bytes)
{
    PutDec((bytes + 512) / 1024);
    Sio_Puts(" KB");
}

/* A hook's code - not the watch's own, and not the startup code. INVOKER and
   init() run once before the game and return; the return addresses their
   frames leave in the stack outlive them, and a scan that took one for a
   live hook frame charged nearly every block to it. */
static bool InPluginCode(unsigned int w)
{
    if (w - (unsigned int)_heapwatch_start < (unsigned int)(_heapwatch_end - _heapwatch_start))
        return false;
    if (w - (unsigned int)_startup_start < (unsigned int)(_startup_end - _startup_start))
        return false;
    return w - PLUGIN_BASE_ADDRESS < PLUGIN_RESERVE_BYTES ||
           w - CAVE2_ADDRESS < CAVE2_RESERVE;
}

static bool InHeapCode(unsigned int w)
{
    return w - HEAP_CODE_START < HEAP_CODE_END - HEAP_CODE_START ||
           w - OPERATOR_NEW_START < OPERATOR_NEW_END - OPERATOR_NEW_START;
}

/* Game or plugin code, with a jal or jalr two instructions before it. */
static bool IsReturnAddress(unsigned int w, bool* plugin)
{
    unsigned int insn;

    if ((w & 3) != 0)
        return false;
    *plugin = InPluginCode(w);
    if (!*plugin && w - GAME_TEXT_START >= GAME_TEXT_END - GAME_TEXT_START)
        return false;
    insn = *(const unsigned int*)(w - 8);
    return (insn >> 26) == 3 || ((insn >> 26) == 0 && (insn & 0x3F) == 9);
}

void __attribute__((optimize("O2"))) HeapWatch_FindCallers(const unsigned int* sp, HeapCallers* c)
{
    int i, n = 0;
    bool plugin;

    c->ra1 = c->ra2 = c->plugin = 0;
    for (i = 0; i < SCAN_WORDS; i++)
    {
        unsigned int w = sp[i];

        if (!IsReturnAddress(w, &plugin))
            continue;
        if (plugin && c->plugin == 0)
            c->plugin = w;
        if (n == 2 || InHeapCode(w))
            continue;
        if (n == 1 && w - (c->ra1 - SAME_FUNCTION) < 2 * SAME_FUNCTION)
            continue;
        if (n == 0)
            c->ra1 = w;
        else
            c->ra2 = w;
        n++;
    }
}

/* Up to `max` callers past the heap's own code, for a failure report. */
static int __attribute__((optimize("O2"))) Chain(const unsigned int* sp, unsigned int* out, int max)
{
    int i, n = 0;
    bool plugin;

    for (i = 0; i < SCAN_WORDS && n < max; i++)
    {
        unsigned int w = sp[i];

        if (!IsReturnAddress(w, &plugin) || InHeapCode(w))
            continue;
        if (n > 0 && w - (out[n - 1] - SAME_FUNCTION) < 2 * SAME_FUNCTION)
            continue;
        out[n++] = w;
    }
    return n;
}

/* The first return address into plugin code, or 0. Runs on every allocation,
   so it looks no further than the plugin ranges. */
static unsigned int __attribute__((optimize("O2"))) PluginReturn(const unsigned int* sp)
{
    int i;
    bool plugin;

    for (i = 0; i < PLUGIN_SCAN; i++)
    {
        unsigned int w = sp[i];

        if (InPluginCode(w) && IsReturnAddress(w, &plugin))
            return w;
    }
    return 0;
}

/* ---- the disc's table of contents ----------------------------------------------- */

/*
    The file device's mount (0x45FC40) reads GT4.VOL's table of contents into
    one heap block and hands it to the volume (0x4618B0), which keeps it for
    the session; lookups inflate its pages from there on demand, into an
    8-page cache the mount makes next (0x461C58). Both calls are taken here,
    on the mount's disc path and its file path alike. Once the cache is up -
    still inside the mount, so nothing else is using the volume yet - every
    page is read through the game's own GetPage (0x461A20), which inflates it
    with a decompressor it builds on its own stack and decrypts it, and the
    record pages' entries are counted by their type. Each page is released
    again at once (0x461A00): GetPage pins its slot in the 8-slot cache until
    then, and once all eight are pinned the next GetPage spins forever looking
    for a free one. What the count leaves in the cache is only a cache. The
    layout is in the target header.
*/

static void __attribute__((optimize("O2"))) CountToc(void)
{
    unsigned int size = s_tocPageSize, files = 0, folders = 0, i, j, n, info;
    const unsigned char* page;

    /* A page size and page count the table itself could hold. */
    if (size < 0x40 || size > 0x8000 || s_toc.pages == 0 ||
        0x40 + (s_toc.pages + 1) * 4 > s_toc.bytes)
        return;

    for (i = 0; i < s_toc.pages; i++)
    {
        page = ((GetPageFn)ADDR_Vol_GetPage)(s_tocVolume, i);
        if (page == 0)
            return;
        if (U16(page) == 0)                      /* records; 1 is an index page */
        {
            n = U16(page + 2);
            n = n != 0 ? (n - 1) / 2 : 0;
            for (j = 0; j < n && 12 + (j + 1) * 8 <= size; j++)
            {
                info = U16(page + size - (j + 1) * 8 + 4);
                if (info >= size)
                    continue;
                if (page[info] == 0)
                    folders++;
                else if (page[info] <= 2)
                    files++;
            }
        }
        ((ReleasePageFn)ADDR_Vol_ReleasePage)(s_tocVolume, i);
    }
    s_toc.files = files;
    s_toc.folders = folders;
}

/* Two lines: what the table holds, then the retail disc's against it. */
static void PutToc(const char* prefix)
{
    Sio_Puts(prefix);
    Sio_Puts("GT4.VOL's table of contents holds ");
    PutKB(s_toc.bytes);
    Sio_Puts(" of heap for the whole session: ");
    if (s_toc.files + s_toc.folders != 0)
    {
        PutDec(s_toc.files);
        Sio_Puts(" files and ");
        PutDec(s_toc.folders);
        Sio_Puts(" folders in ");
    }
    PutDec(s_toc.pages);
    Sio_Puts(" pages\n");

    Sio_Puts(prefix);
    Sio_Puts("  the retail disc's holds ");
    PutKB(TOC_RETAIL_BYTES);
    Sio_Puts(" for ");
    PutDec(TOC_RETAIL_FILES);
    Sio_Puts(" files and ");
    PutDec(TOC_RETAIL_FOLDERS);
    Sio_Puts(" folders - ");
    if (s_toc.bytes >= TOC_RETAIL_BYTES + 512)
    {
        Sio_Puts("this disc costs every scene ");
        PutKB(s_toc.bytes - TOC_RETAIL_BYTES);
        Sio_Puts(" of heap\n");
    }
    else if (s_toc.bytes + 512 <= TOC_RETAIL_BYTES)
    {
        Sio_Puts("this disc leaves every scene ");
        PutKB(TOC_RETAIL_BYTES - s_toc.bytes);
        Sio_Puts(" more heap\n");
    }
    else
        Sio_Puts("the same as this disc's\n");
}

/* In place of the mount's calls to 0x4618B0, which hand the volume its table:
   notes the table, then hands it over. */
unsigned int HeapWatch_SetTable(void* volume, const unsigned char* table, unsigned int bytes)
{
    s_tocVolume = volume;
    s_toc.bytes = bytes;
    s_toc.pages = U16(table + 0x12);
    s_toc.files = 0;
    s_toc.folders = 0;
    s_tocPageSize = U16(table + 0x10);
    return ((SetTableFn)ADDR_Vol_SetTable)(volume, table, bytes);
}

/* In place of the mount's calls to 0x461C58, which make the volume's page
   cache: once it is there, counts the table and says what it costs. */
unsigned int HeapWatch_CacheInit(void* cache, unsigned int pageSize, void* buffer,
                                 unsigned int bytes)
{
    unsigned int r = ((CacheInitFn)ADDR_Vol_CacheInit)(cache, pageSize, buffer, bytes);

    if (s_tocVolume != 0 && (unsigned char*)cache == (unsigned char*)s_tocVolume + VOL_PAGE_CACHE)
    {
        CountToc();
        PutToc("[gt4hooks] toc: ");
    }
    return r;
}

const HeapToc* HeapWatch_Toc(void)
{
    return &s_toc;
}

/* ---- the hooks' blocks ------------------------------------------------------ */

static unsigned int TrackHome(unsigned int block)
{
    return ((block >> 4) * 2654435761u) >> (32 - TRACK_BITS);
}

static int SiteFor(unsigned int site)
{
    int i;

    for (i = 0; i < s_nSites && s_sites[i].site != site; i++)
        ;
    if (i == s_nSites)
    {
        if (s_nSites < SITE_MAX)
            s_sites[s_nSites++].site = site;
        else
            i = SITE_MAX - 1;          /* the last entry takes the overflow */
    }
    return i;
}

static void __attribute__((optimize("O2"))) Track(unsigned int block, unsigned int size,
                                                   unsigned int site)
{
    unsigned int i = TrackHome(block);
    int k;

    if (s_nTrack >= TRACK_LIMIT)
    {
        s_full = true;
        return;
    }
    while (s_track[i].block != 0)
        i = (i + 1) & (TRACK_SLOTS - 1);
    k = SiteFor(site);
    s_track[i].block = block;
    s_track[i].size = size;
    s_track[i].site = (unsigned int)k;
    s_nTrack++;
    s_sites[k].live += (int)size;
    s_sites[k].blocks++;
    s_hookLive += (int)size;
    if (s_hookLive > s_hookMost)
        s_hookMost = s_hookLive;

    if (s_hookLive / REPORT_STEP > s_reported)
    {
        s_reported = s_hookLive / REPORT_STEP;
        Sio_Puts("[gt4hooks] heap: the hooks now hold ");
        PutHex((unsigned int)s_hookLive);
        Sio_Puts(" bytes in ");
        PutHex((unsigned int)s_nTrack);
        Sio_Puts(" blocks; this one for ");
        PutHex(site);
        Sio_Puts("\n");
    }
}

static void __attribute__((optimize("O2"))) Untrack(unsigned int block)
{
    unsigned int i = TrackHome(block), j, k;

    while (s_track[i].block != block)
    {
        if (s_track[i].block == 0)
            return;
        i = (i + 1) & (TRACK_SLOTS - 1);
    }
    s_sites[s_track[i].site].live -= (int)s_track[i].size;
    s_sites[s_track[i].site].blocks--;
    s_hookLive -= (int)s_track[i].size;
    s_nTrack--;

    /* Close the gap, so later entries stay reachable. */
    j = i;
    for (;;)
    {
        j = (j + 1) & (TRACK_SLOTS - 1);
        if (s_track[j].block == 0)
            break;
        k = TrackHome(s_track[j].block);
        if (i <= j ? (k <= i || k > j) : (k <= i && k > j))
        {
            s_track[i] = s_track[j];
            i = j;
        }
    }
    s_track[i].block = 0;
    if (s_hookLive / REPORT_STEP < s_reported)
        s_reported = s_hookLive / REPORT_STEP;
}

static void PrintHooks(void)
{
    int shown[4];
    int k, i, best;

    Sio_Puts("[gt4hooks] heap:   the hooks hold ");
    PutHex((unsigned int)s_hookLive);
    Sio_Puts(" bytes in ");
    PutHex((unsigned int)s_nTrack);
    Sio_Puts(" blocks (most ");
    PutHex((unsigned int)s_hookMost);
    Sio_Puts(s_full ? "; table full, so low)\n" : ")\n");

    for (k = 0; k < 4 && s_sites != 0; k++)
    {
        best = -1;
        for (i = 0; i < s_nSites; i++)
        {
            if (s_sites[i].live <= 0 || (k > 0 && i == shown[0]) ||
                (k > 1 && i == shown[1]) || (k > 2 && i == shown[2]))
                continue;
            if (best < 0 || s_sites[i].live > s_sites[best].live)
                best = i;
        }
        if (best < 0)
            break;
        shown[k] = best;
        Sio_Puts("[gt4hooks] heap:     ");
        PutHex(s_sites[best].site);
        Sio_Puts(": ");
        PutHex((unsigned int)s_sites[best].live);
        Sio_Puts(" bytes in ");
        PutHex((unsigned int)s_sites[best].blocks);
        Sio_Puts(" blocks\n");
    }
}

/* malloc found no block. `needed` is the whole block, header included. */
static void Fail(unsigned int needed, unsigned int align, const unsigned int* sp)
{
    unsigned int chain[CHAIN_MAX];
    int n, i;

    n = Chain(sp, chain, CHAIN_MAX);
    if (needed == s_lastNeeded && (n > 0 ? chain[0] : 0) == s_lastRa1)
        return;                            /* the blocking wrappers retrying */
    s_lastNeeded = needed;
    s_lastRa1 = n > 0 ? chain[0] : 0;

    Sio_Puts("[gt4hooks] heap: an allocation FAILED - ");
    PutHex(needed - 16);
    Sio_Puts(" bytes, align ");
    PutHex(align);
    Sio_Puts(", for");
    for (i = 0; i < n; i++)
    {
        Sio_Puts(i == 0 ? " " : " <- ");
        PutHex(chain[i]);
    }
    Sio_Puts("; largest free ");
    PutHex((unsigned int)((PoolQueryFn)ADDR_Pool_LargestFree)((void*)POOL_OBJECT));
    Sio_Puts(", total free ");
    PutHex((unsigned int)((PoolQueryFn)ADDR_Pool_TotalFree)((void*)POOL_OBJECT));
    Sio_Puts("\n");
    if (s_toc.bytes != 0)
        PutToc("[gt4hooks] heap:   ");
    PrintHooks();
}

/* From HeapWatch_AllocSeen, with the pool locked, on every allocation. */
void HeapWatch_OnAlloc(unsigned int block, unsigned int needed, unsigned int align,
                       const unsigned int* sp, unsigned int pool)
{
    unsigned int site;

    if (pool != POOL_OBJECT)
        return;
    if (block == 0)
    {
        Fail(needed, align, sp);
        return;
    }
    if (s_track == 0)
        return;
    site = PluginReturn(sp);
    if (site != 0)
        Track(block, ((unsigned int*)block)[1] - block, site);
}

/* From HeapWatch_FreeSeen, with the pool locked. */
void HeapWatch_OnFree(unsigned int block, unsigned int pool)
{
    if (pool == POOL_OBJECT && s_nTrack != 0)
        Untrack(block);
}

/*
    The two stand-ins, called in place of the pool routines' lock calls:

    HeapWatch_AllocSeen  instead of malloc's unlock at 0x5275BC: a0 is the lock,
                         s4 the new block (0 if none), s5 the alignment, s6 the
                         pool, s7 the block size wanted, sp the pool routine's
                         frame. Reports the allocation, then unlocks.
    HeapWatch_FreeSeen   instead of free's lock at 0x5277C4: a0 is the lock, s1
                         the block, s2 the pool. Locks, then reports the free.

    Both leave the pool routine's registers as its own call would.
*/
void HeapWatch_AllocSeen(void);
void HeapWatch_FreeSeen(void);

__asm__(
    ".pushsection .text\n"
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".align 2\n"
    ".globl HeapWatch_AllocSeen\n"
    ".type HeapWatch_AllocSeen, @function\n"
    "HeapWatch_AllocSeen:\n"
    "    addiu $sp, $sp, -32\n"
    "    sd    $ra, 0($sp)\n"
    "    sd    $a0, 8($sp)\n"
    "    move  $a0, $s4\n"
    "    move  $a1, $s7\n"
    "    move  $a2, $s5\n"
    "    addiu $a3, $sp, 32\n"
    "    jal   HeapWatch_OnAlloc\n"
    "    move  $8, $s6\n"
    "    ld    $a0, 8($sp)\n"
    "    ld    $ra, 0($sp)\n"
    "    li    $t9, " HW_STR(ADDR_Pool_Unlock) "\n"
    "    jr    $t9\n"
    "    addiu $sp, $sp, 32\n"
    ".size HeapWatch_AllocSeen, .-HeapWatch_AllocSeen\n"
    ".globl HeapWatch_FreeSeen\n"
    ".type HeapWatch_FreeSeen, @function\n"
    "HeapWatch_FreeSeen:\n"
    "    addiu $sp, $sp, -32\n"
    "    sd    $ra, 0($sp)\n"
    "    li    $t9, " HW_STR(ADDR_Pool_Lock) "\n"
    "    jalr  $t9\n"
    "    nop\n"
    "    move  $a0, $s1\n"
    "    jal   HeapWatch_OnFree\n"
    "    move  $a1, $s2\n"
    "    ld    $ra, 0($sp)\n"
    "    jr    $ra\n"
    "    addiu $sp, $sp, 32\n"
    ".size HeapWatch_FreeSeen, .-HeapWatch_FreeSeen\n"
    ".set pop\n"
    ".popsection\n");

#if !PLATFORM_PCSX2
/* Stands in for PDISTD::FileDevice::readStream (its vtable slot): notes a read
   into a buffer that cannot be real, then reads as before. */
int HeapWatch_ReadStream(FileDevice* device, FileInternalStream* stream, MemorySpace* memSpace,
                         int expand)
{
    if (memSpace != 0 && memSpace->BufferSize > 0 &&
        (unsigned int)memSpace->BufferPtr < GAME_TEXT_START)
    {
        Sio_Puts("[gt4hooks] read: ");
        if (stream != 0 && stream->FileObject != 0)
        {
            char* name = PlaystationX_LockFileName(stream->FileObject->FileNameHandle);

            Sio_Puts(name);
            PlayStationX_UnlockFileName(stream->FileObject->FileNameHandle);
        }
        Sio_Puts(" into buffer ");
        PutHex((unsigned int)memSpace->BufferPtr);
        Sio_Puts(", ");
        PutHex((unsigned int)memSpace->BufferSize);
        Sio_Puts(" bytes, for ");
        PutHex((unsigned int)__builtin_return_address(0));
        Sio_Puts("\n");
    }
    return s_readStream(device, stream, memSpace, expand);
}
#endif

void HeapWatch_Install(void)
{
    unsigned int scratch = ((unsigned int)_cave2b_end + 15) & ~15u;
    unsigned int need = TRACK_SLOTS * sizeof(Tracked) + SITE_MAX * sizeof(Site);
    unsigned int* p;

    if (*(volatile unsigned int*)ADDR_PoolAlloc_UnlockCall == INSN_JAL_Pool_Unlock &&
        *(volatile unsigned int*)(ADDR_PoolAlloc_UnlockCall + 4) == INSN_PoolAlloc_UnlockSlot &&
        *(volatile unsigned int*)ADDR_PoolFree_LockCall == INSN_JAL_Pool_Lock &&
        *(volatile unsigned int*)(ADDR_PoolFree_LockCall + 4) == INSN_PoolFree_LockSlot)
    {
        /* The hooks' tables go in the dead .cave2b run, past anything linked
           there; without the room, failures are still reported. */
        if (scratch + need <= CAVE2B_ADDRESS + CAVE2B_RESERVE)
        {
            for (p = (unsigned int*)scratch; p < (unsigned int*)(scratch + need); p++)
                *p = 0;
            s_sites = (Site*)(scratch + TRACK_SLOTS * sizeof(Tracked));
            s_track = (Tracked*)scratch;
        }
        MAKE_JAL(ADDR_PoolAlloc_UnlockCall, &HeapWatch_AllocSeen);
        MAKE_JAL(ADDR_PoolFree_LockCall,    &HeapWatch_FreeSeen);
    }

    /* The table of contents, whoever has the allocator sites. */
    if (*(volatile unsigned int*)ADDR_VolMount_SetTableDisc == INSN_JAL_Vol_SetTable &&
        *(volatile unsigned int*)ADDR_VolMount_SetTableFile == INSN_JAL_Vol_SetTable &&
        *(volatile unsigned int*)ADDR_VolMount_CacheDisc == INSN_JAL_Vol_CacheInit &&
        *(volatile unsigned int*)ADDR_VolMount_CacheFile == INSN_JAL_Vol_CacheInit)
    {
        MAKE_JAL(ADDR_VolMount_SetTableDisc, &HeapWatch_SetTable);
        MAKE_JAL(ADDR_VolMount_SetTableFile, &HeapWatch_SetTable);
        MAKE_JAL(ADDR_VolMount_CacheDisc,    &HeapWatch_CacheInit);
        MAKE_JAL(ADDR_VolMount_CacheFile,    &HeapWatch_CacheInit);
    }

#if !PLATFORM_PCSX2
    {
        unsigned int orig = *(volatile unsigned int*)ADDR_PDISTD_FileDevice_readStream;

        if (orig - GAME_TEXT_START < GAME_TEXT_END - GAME_TEXT_START)
        {
            s_readStream = (ReadStreamFn)orig;
            HOOK_FUNC_ADDR((void*)ADDR_PDISTD_FileDevice_readStream, &HeapWatch_ReadStream);
        }
    }
#endif
}
