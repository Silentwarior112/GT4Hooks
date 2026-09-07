#include <stdbool.h>
#include <stdio.h>

#include "MStorage.h"
#include "Adhoc.h"                 /* STD_STRING, ADHOC_MakeFloat */
#include "core/ps2/Memory.h"
#include "core/Log.h"
#include "core/util/String.h"
#include "util/Path.h"

#include "core/game/String.h"
#include "GameFunctions/MStorage.h"
#include "core/game/IO.h"
#include "GameFunctions/Monitor.h"

/* Hook: MStorage
   Purpose: Allows reading files from memory card from adhoc.
   How: Fixes MStorageMC::getFileSize being unimplemented so that MStorage::read works (which uses it)
   Adhoc Usage:
     #define STORAGE_HD 0
     #define STORAGE_MC 1

     var storage = main::menu::MStorage::getStorage(STORAGE_MC);
     var str = storage.read("/BASCUS-97436GAMEDATA/file.txt");
     print "%{str}\n";

   Files do NOT need to be NUL-terminated. read() builds its string from
   strlen() of a buffer that nothing zeroes, so left alone it would run off the
   end of the file - but getFileSize here reports one extra byte and
   HOOK__mStorageMC_read writes the terminator into it, so plain text files
   work and write()/read() round trips return exactly what was written.

   The directory must already exist. mStorageMC::mkdir (0x3DF560) is a stub -
   `jr $ra; move $v0, $zero` - so storage.mkdir() creates nothing and still
   returns 0. The file itself does not need to pre-exist: the manager retries
   sceMcOpen with the create flag at 0x514A60 when the first open returns -4.
*/

/* The real byte size of a file on the card, or -1. Defined below - declared
   here because the write hook needs it and must NOT use the getFileSize hook,
   which deliberately reports one byte more. */
static int MC_FileSizeBytes(void* this, const char* name);

void MStorage_InstallHooks()
{
    HOOK(ADDR_mStorageMC_getFileSize, &HOOK__mStorageMC_getFileSize);
    HOOK(ADDR_mStorageMC_read, &HOOK__mStorageMC_read);
#if MSTORAGE_WRITE_TRUNCATES
    HOOK(ADDR_mStorageMC_write, &HOOK__mStorageMC_write);
#endif

#if MSTORAGE_EXTRA_MEMBERS
    /* Redirect the module's last member registration so we can add our own
       alongside it - see HOOK_ExtendMStorage. */
    MAKE_JAL(ADDR_MStorage_mkdir_register, &HOOK_ExtendMStorage);
#endif
}

#if MSTORAGE_EXTRA_MEMBERS
/*
    Add the plugin's own members to MStorage.

    This replaces the CALL that registers "mkdir", the last member in the
    module's InitClass, so we re-register mkdir ourselves and then add whatever
    else we want. Being last means our members land after every stock one.

    The dtor discipline is the fiddly part. Each define* call constructs a
    handle into the caller's HValue slot and every one of them needs releasing.
    The game emits exactly one HValue_dtor after this call (0x3B7500), so it
    accounts for the LAST registration here - which means we must release every
    other one ourselves, and must NOT release the last.

    Note tempHValue is passed straight through, not by address: the caller does
    `move $a0, $sp` at 0x3B74E8, so it already IS the slot's address.
*/
void HOOK_ExtendMStorage(void* tempHValue, hModule* module, char* name, Adhoc_method_cb method)
{
    /* The registration we displaced. In mode 1 this is ALL we do, so the
       game's own dtor at 0x3B7500 balances it exactly and the module ends up
       with precisely the members it started with - the only difference from
       stock being that the call went through here. */
    hModule_defineMethod(tempHValue, module, name, method);

    /* Ours. The game's own HValue_dtor at 0x3B7500 releases the last one. */
    hModule_defineMethod(tempHValue, module, "ping", &m_ping);
    hModule_defineMethod(tempHValue, module, "echoVar", &m_echoVar);
    hModule_defineMethod(tempHValue, module, "setVar", &m_setVar);
    hModule_defineMethod(tempHValue, module, "getVar", &m_getVar);
    hModule_defineMethod(tempHValue, module, "delVar", &m_delVar);
    hModule_defineMethod(tempHValue, module, "setVars", &m_setVars);
    hModule_defineMethod(tempHValue, module, "getVars", &m_getVars);
    hModule_defineMethod(tempHValue, module, "loadVars", &m_loadVars);
    hModule_defineMethod(tempHValue, module, "saveVars", &m_saveVars);

    /* Functions, not methods: no receiver, so a race-context script can call
       them without building a storage object. */
    hModule_defineFunction(tempHValue, module, "getExtraData", &f_getExtraData);
    hModule_defineFunction(tempHValue, module, "setExtraData", &f_setExtraData);

    /* NOTE, because this differs from MCarGarage.c and the difference matters:
       there must be NO HValue_dtor between the two registrations here.

       MCarGarage's equivalent hook releases the slot between its two calls, and
       that module works. Doing the same here crashed the game at boot, every
       time, and bisecting it three ways settled which part:

         redirect the call, register nothing extra   -> boots
         register ping, WITHOUT the dtor             -> boots
         register ping, WITH the dtor                -> crashes at boot

       So the extra registration is fine and the hook mechanism is fine; the
       release is what kills it. The slot write at 0x50C300 is a construct
       rather than an assign - it overwrites without unref-ing and then refs the
       new value - so skipping the release costs at most one reference on a
       handle whose module lives for the whole session. That is a trade worth
       making against a boot crash. */
}

/*
    MStorage::ping() -> 42

    Deliberately trivial: it proves the registration hook fired, that the name
    resolves from script, that the method ABI is right and that the handle
    dance around the return value is correct - all without touching the card.
*/
void m_ping(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    /* As in MMyModule.c: the constructor fills a handle slot, so it takes the
       ADDRESS of the handle variable, and the dtor does too. */
    HInt* result;
    HInt_HInt((HInt*)&result, 42);

    ADHOC_MakeRef(return_value, (HObject*)result);

    HInt_dtor((HInt*)&result, 2);
}

/* ---- adhoc value conversion ---------------------------------------------

   Every adhoc value answers the same conversion calls at fixed vtable offsets,
   so these work without knowing a value's type. `slot` is the ADDRESS of the
   word holding the object, which for argument N is argv + N - the same thing
   the game passes itself (hObject::toInt at 0x4F2E88 does `lw $a2, ($a1)`).
*/

static void* Adhoc_VTableOf(hObject** slot)
{
    char* obj = *(char**)slot;
    return obj ? *(void**)(obj + 4) : NULL;
}

static int Adhoc_ToInt(hObject** slot)
{
    char* obj = *(char**)slot;
    char* entry = (char*)(*(void**)(obj + 4)) + ADHOC_VT_TOINT;
    int (*fn)(void*) = *(int (**)(void*))(entry + 4);
    return fn(obj + *(short*)entry);
}

static float Adhoc_ToFloat(hObject** slot)
{
    char* obj = *(char**)slot;
    char* entry = (char*)(*(void**)(obj + 4)) + ADHOC_VT_TOFLOAT;
    float (*fn)(void*) = *(float (**)(void*))(entry + 4);
    return fn(obj + *(short*)entry);
}

static char* Adhoc_CStr(hObject** slot)
{
    char* obj = *(char**)slot;
    char* entry = (char*)(*(void**)(obj + 4)) + ADHOC_VT_CSTR;
    char* (*fn)(void*) = *(char* (**)(void*))(entry + 4);
    return fn(obj + *(short*)entry);
}

/* Build an HString handle from C text. The handle is the caller's to release. */
static void Adhoc_MakeString(HString** slot, char* text)
{
    STD_STRING(text);

    HString_HString((HString*)slot, &unk);

    /* Release our reference on the std::string, as Decl.h's module
       boilerplate does - the handle took its own. */
    {
        void* rep = (char*)unk - 16;
        int refs = *((int*)unk - 2) - 1;

        *((int*)unk - 2) = refs;

        if (refs == 0)
            ADHOC_Deallocate(rep, (void*)(*((int*)rep + 1) + 16), 4);
    }
}

/* Parse text the way adhoc itself does: through hString's conversion slots.
   Keeps atoi/atof - and every C double - out of the plugin. */
static int Adhoc_ParseInt(char* text)
{
    HString* s;
    int value;

    Adhoc_MakeString(&s, text);
    value = Adhoc_ToInt((hObject**)&s);
    HValue_dtor((void*)&s, 2);

    return value;
}

static float Adhoc_ParseFloat(char* text)
{
    HString* s;
    float value;

    Adhoc_MakeString(&s, text);
    value = Adhoc_ToFloat((hObject**)&s);
    HValue_dtor((void*)&s, 2);

    return value;
}

/*
    A float's exact 32 bits as "0xdeadbeef", and back. Written by hand rather
    than with "%08x" because this build's printf has already been caught
    ignoring a width, and this is the form that has to be exact.
*/
static void Adhoc_WriteHexBits(char* out, unsigned int bits)
{
    static const char digits[] = "0123456789abcdef";

    int i;

    out[0] = '0';
    out[1] = 'x';

    for (i = 0; i < 8; i++)
        out[2 + i] = digits[(bits >> (28 - i * 4)) & 0xF];

    out[10] = 0;
}

static int Adhoc_ReadHexBits(const char* text, unsigned int* bits)
{
    unsigned int value = 0;
    int i;

    if (text[0] != '0' || (text[1] != 'x' && text[1] != 'X'))
        return 0;

    for (i = 0; i < 8; i++)
    {
        char c = text[2 + i];
        unsigned int digit;

        if      (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return 0;

        value = (value << 4) | digit;
    }

    if (text[10] != 0)
        return 0;

    *bits = value;
    return 1;
}

/*
    Render a float as the shortest text that reads back as the SAME float.
    Returns 1 if that text is decimal, 0 if it had to fall back to raw hex.

    Two separate things stop a single _sprintf from being enough.

    This build's %g does not follow the precision it is given - "%.7g" rendered
    1.351f as "1.3509999" (eight digits) but 1.23e-7f as "1.23e-07" (three), so
    a precision cannot be chosen ahead of time. And the game's decimal-to-float
    path - hString::toFloat -> atof -> the double narrowing at 0x557620 - lands
    one ULP low on values whose text sits at or below the float, no matter how
    many digits it is handed: even "%.17g" of 1.2345678e20f came back one bit
    short. More digits cannot correct a converter that falls the wrong way.

    So: format, parse it back, and accept only a form that reproduces the value
    bit for bit. Short forms are tried first, which keeps the file readable and
    also happens to suit the parser - a short form rounds UP past the float, and
    the parser's fall then lands on it. Every precision is tried rather than a
    sampled few, because which ones round up is a property of the value. If none
    does, the value is offered again nudged up by one ULP (adding 1 to the bit
    pattern raises the magnitude for either sign) so the fall lands where we
    want it. Failing even that, the exact bits go down as hex.

    Verifying rather than trusting is the point. Whatever this printf and this
    atof do between them, the text written here reads back as the same float, so
    a value cannot drift through repeated read-modify-write cycles.

    `out` must be at least 32 bytes.
*/
static int Adhoc_FormatFloat(char* out, float value)
{
    static const char* const forms[] = {
        "%.1g",  "%.2g",  "%.3g",  "%.4g",  "%.5g",  "%.6g",
        "%.7g",  "%.8g",  "%.9g",  "%.10g", "%.11g", "%.12g",
        "%.13g", "%.14g", "%.15g", "%.16g", "%.17g"
    };

    unsigned int want = *(unsigned int*)&value;
    unsigned int nudge;

    /* Positive zero only. Negative zero deliberately goes the long way round:
       skipping the loop for it would write "0" and lose the sign bit, making
       -0.0f the one value that does NOT round trip - the exact property this
       function exists to guarantee. It costs nothing to let it try "-0" and
       fall through to the hex form if this atof drops the sign. */
    if (want == 0)
    {
        __strcpy(out, "0");
        return 1;
    }

    for (nudge = 0; nudge <= 2; nudge++)
    {
        unsigned int bits = want + nudge;
        float candidate = *(float*)&bits;
        unsigned int i;

        /* Nudged off the end of the finite range - nothing left to try. */
        if ((bits & 0x7F800000) == 0x7F800000)
            break;

        for (i = 0; i < sizeof(forms) / sizeof(forms[0]); i++)
        {
            float back;

            _sprintf(out, forms[i], (double)candidate);
            back = Adhoc_ParseFloat(out);

            if (*(unsigned int*)&back == want)
                return 1;
        }
    }

    Adhoc_WriteHexBits(out, want);
    return 0;
}

/* The other half of Adhoc_FormatFloat: decimal or hex, whichever it wrote. */
static float Adhoc_ReadFloat(char* text)
{
    unsigned int bits;

    if (Adhoc_ReadHexBits(text, &bits))
        return *(float*)&bits;

    return Adhoc_ParseFloat(text);
}

/* 'i', 'f', 's', or '?' for anything else - nil included. */
static char Adhoc_TypeLetter(hObject** slot)
{
    void* vt = Adhoc_VTableOf(slot);

    if (vt == (void*)ADDR_hInt_VTable)    return 'i';
    if (vt == (void*)ADDR_hFloat_VTable)  return 'f';
    if (vt == (void*)ADDR_hString_VTable) return 's';

    return '?';
}

/* ---- adhoc arrays ---------------------------------------------------------

   Three accessors, and deliberately no engine calls. hArray has perfectly good
   getElement and setElement members and both are unsafe to hand a plugin's
   arithmetic:

     hArray::getElement (0x4E0110) does compare the index, at 0x4E0130 - and
     then reports the failure to ADHOC_printf (0x6259D8), which in this build is
     a stripped stub that spills its argument registers and returns. It then
     FALLS THROUGH and hands back the out-of-range pointer anyway. No throw, no
     early return, no diagnostic.

     hArray::setElement (0x4E01D0) is worse. Its bounds test at 0x4E01F4 is
     UNSIGNED, and out of range it does not fail - it calls the grow helper at
     0x4DFCF8, which rounds up to the next power of two and resizes. Index 100
     on a three-element array silently produces a 128-element array full of null
     handles, and a null handle faults the next `if (x)` a script performs on it.

   So the index arithmetic is ours and it is bounded here. Nothing is given up
   by not calling setElement: the store below is the same ref / unref / write it
   performs at 0x4E022C, 0x4E0240 and 0x4E0248.
*/

/* Total - a NULL slot, a null object (adhoc's own nil IS a null handle) and a
   wrong vtable all answer 0. Adhoc_VTableOf guards the object but not the
   slot, so the slot is checked here. */
static int Adhoc_IsArray(hObject** slot)
{
    return slot != NULL && Adhoc_VTableOf(slot) == (void*)ADDR_hArray_VTable;
}

/* hArray::size (0x4E0028), inlined. Precondition: Adhoc_IsArray(slot).

   A freshly constructed array has start == finish == NULL, which the explicit
   test below handles rather than leaving to a subtraction of two null pointers. */
static int Adhoc_ArrayCount(hObject** slot)
{
    char*     arr    = *(char**)slot;
    hObject** start  = *(hObject***)(arr + ADHOC_ARRAY_START);
    hObject** finish = *(hObject***)(arr + ADHOC_ARRAY_FINISH);

    if (start == NULL || finish == NULL)
        return 0;

    return (int)(finish - start);
}

/* The ADDRESS of the word holding element i, which is exactly the shape every
   Adhoc_ helper above takes - feed it straight to Adhoc_TypeLetter, Adhoc_CStr,
   Adhoc_IsArray or KV_ValueText. NULL when i is out of range, because the
   engine's own accessor will not tell you. Precondition: Adhoc_IsArray(slot). */
static hObject** Adhoc_ArrayElem(hObject** slot, int i)
{
    char* arr;

    if (i < 0 || i >= Adhoc_ArrayCount(slot))
        return NULL;

    arr = *(char**)slot;

    return *(hObject***)(arr + ADHOC_ARRAY_START) + i;
}

/*
    Replace the value an element slot holds, in place.

    `object` is what a handle variable HOLDS after HInt_HInt / ADHOC_MakeFloat /
    Adhoc_MakeString - the same thing m_ping passes to ADHOC_MakeRef - and NOT
    the address of the handle. Getting that backwards refs a stack slot as if it
    were a reference-counted object and stores a stack address into a live
    array, which faults later and far away.

    Written before the old value is released, not after. Releasing first would
    leave `dest` pointing into memory that the outgoing object's destructor
    could have freed - it can run arbitrary engine code, including hArray's own
    recursive destructor - and the store would then land on freed memory. This
    order also makes replacing a value with itself harmless.
*/
static void Adhoc_ArrayStore(hObject** dest, HObject* object)
{
    hObject* old = *dest;

    if (object != NULL)
        RefCounter_ref((RefCounter*)object);

    *dest = (hObject*)object;

    if (old != NULL)
        RefCounter_unref((RefCounter*)old);
}

void m_echoVar(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    if (argc < 1)
        return;

    char type = Adhoc_TypeLetter(argv);
    char text[96] = {0};

    if (type == 'i')
    {
        _sprintf(text, "%d", Adhoc_ToInt(argv));
    }
    else if (type == 'f')
    {
        /* Not hFloat's own toString: that formats with "%f" into a 32-byte
           buffer, which a large value overruns. */
        if (!Adhoc_FormatFloat(text, Adhoc_ToFloat(argv)))
            LOG("echoVar: no decimal form of this float survives the parser; stored as raw bits\n");
    }
    else if (type == 's')
    {
        char* s = Adhoc_CStr(argv);

        if (s == NULL || __strlen(s) >= (int)sizeof(text))
        {
            LOG("echoVar: string missing or too long for the round trip\n");
            return;
        }

        __strcpy(text, s);
    }
    else
    {
        LOG("echoVar: unrecognised value type\n");
        return;
    }

    LOG("echoVar: type=%c text='%s'\n", type, text);

    /* Parse it back from the text, so this really is the round trip the stored
       format will perform - not a shortcut that returns the original value. */
    if (type == 'i')
    {
        HInt* result;
        HInt_HInt((HInt*)&result, Adhoc_ParseInt(text));
        ADHOC_MakeRef(return_value, (HObject*)result);
        HInt_dtor((HInt*)&result, 2);
    }
    else if (type == 'f')
    {
        HFloat* result;
        ADHOC_MakeFloat(&result, Adhoc_ReadFloat(text));
        ADHOC_MakeRef(return_value, (HObject*)result);
        HFloat_dtor((HFloat*)&result, 2);
    }
    else
    {
        HString* result;
        Adhoc_MakeString(&result, text);
        ADHOC_MakeRef(return_value, (HObject*)result);
        HValue_dtor((void*)&result, 2);
    }
}

/* ---- typed variables in a memory-card file --------------------------------

   setVar/getVar keep named values of the three adhoc value types in one text
   file on the card:

     var storage = main::menu::MStorage::getStorage(1);
     storage.setVar("/BASCUS-97436GAMEDATA/vars.txt", "laps",   5);
     storage.setVar("/BASCUS-97436GAMEDATA/vars.txt", "best",   1.351);
     storage.setVar("/BASCUS-97436GAMEDATA/vars.txt", "driver", "Ayrton");

     var laps = storage.getVar("/BASCUS-97436GAMEDATA/vars.txt", "laps", 0);

   setVar returns 1 or 0. getVar returns the value with the type it was stored
   as, or the third argument if one was given, or nil.

   THE FILE. One line per variable, fields separated by tabs:

     #gt4kv 1
     laps<TAB>i<TAB>5
     best<TAB>f<TAB>1.351
     driver<TAB>s<TAB>Ayrton

   The parser splits on the FIRST tab for the name and requires a type letter
   and a second tab straight after it; everything from there to the newline is
   the value. So a tab inside a value is never examined - the delimiter is only
   special in the first two fields - and values need no escaping.

   THE TYPE LETTER IS NOT REDUNDANT, though the value text often looks
   self-describing. hString::toInt (0x4FB050) is not a plain atoi: it sniffs for
   an 0x prefix at 0x4FB084/0x4FB094 and routes to strtol base 16, so the string
   "0x340411fc" reads as the integer 872423420 - and that is byte for byte what
   Adhoc_FormatFloat emits for a float that has no usable decimal form. Without
   the letter that one line is three different values depending on who asks.
   A string whose content is "5" has the same problem more cheaply.

   THE MAGIC LINE IS A WRITE GUARD, not versioning. setVar rewrites the whole
   file, on the same card as the player's saves, at a path a SCRIPT supplies. A
   mistyped path pointing at a real save would otherwise overwrite its front and
   zero the rest. So setVar refuses to touch an existing file that does not open
   with "#gt4kv 1". getVar does not check - reads harm nothing - which also
   makes '#' a comment marker for hand-edited files.

   Names may not begin with '#' for that reason, and neither a name nor a string
   value may contain a byte below 0x20 or 0x7F. That one rule covers NUL, tab,
   newline and carriage return together. Bytes >= 0x80 pass, so Shift-JIS stores
   fine - no byte of a Shift-JIS pair is ever 0x09 or 0x0A.

   WHAT IT CANNOT DO. Storing anything other than an int, float or string
   returns 0. delVar removes a variable from the file, but the card has neither
   delete nor truncate of its own, so the file never gets physically smaller -
   the removed bytes stay there, past the terminator, dead.
*/

#define KV_SEP        '\t'
#define KV_MAGIC      "#gt4kv 1\n"
#define KV_MAGIC_LEN  9
#define KV_NAME_MAX   63
#define KV_VALUE_MAX  255
#define KV_LINE_MAX   (KV_NAME_MAX + 1 + 1 + 1 + KV_VALUE_MAX + 1)
#define KV_FILE_MAX   4096

/*
    The whole file. Static rather than a local because a method callback runs on
    a menu thread with a 0x8000-byte stack (PDISTD::Thread::create 0x525288) and
    nothing bounds how deep the adhoc interpreter already is when it calls us -
    it recurses per expression node through vtable slots. Four kilobytes there
    is not a gamble worth taking.

    Static rather than allocated because a static cannot fail. The game's own
    read callback calls the pool allocator at 0x3B6BF8 and does not check the
    result before strlen-ing it; we would have to check, and then unwind that
    failure down every exit path, to buy nothing.

    The `= {0}` is load bearing. The makefile builds with
    -fno-zero-initialized-in-bss, so an explicitly zeroed static lands in .data
    and is present in the injected image; an uninitialised one would go to .bss,
    which is not in the file and which nothing here is known to zero.

    Two bytes of slack: the load over-reads by one to detect an oversized file,
    and setVar writes its own terminator one past the content.
*/
static char g_kvFile[KV_FILE_MAX + 2] = {0};

/* One buffer, so one user at a time. */
static int g_kvBusy = 0;

/* ---- the retained copy ----------------------------------------------------

   For every member above, g_kvFile is scratch: load, edit, write, forget. That
   works while a script is holding the values in its own variables, and stops
   working the moment it is not - adhoc tears a context's variables down when
   the game leaves it, so anything loaded in a menu is gone by the time a race
   starts.

   The plugin's own memory does not go anywhere. It is written once when the
   image loads and the game never touches it again, so it outlives menus, races
   and everything between them. Keeping the file's TEXT here, and building adhoc
   values from it on demand, gives a script somewhere to put data that survives
   a state change.

   Text rather than adhoc objects, deliberately. A value built here would be a
   handle owned by a VM context, and holding one across a teardown would leave a
   dangling pointer behind. Bytes cannot dangle.
*/
static char g_kvCachePath[128] = {0};
static int  g_kvCacheUsed  = 0;   /* how much of g_kvFile is content        */
static int  g_kvCacheRaw   = 0;   /* what the load saw physically           */
static int  g_kvCachePadTo = 0;   /* how far a save has to blank            */
static int  g_kvCacheValid = 0;
static int  g_kvCacheDirty = 0;   /* edited in memory, not yet written      */

/*
    Give up the retained copy, because the buffer behind it is about to change.

    Every file-backed member reloads g_kvFile, so any of them run between a
    loadVars and a saveVars silently discards edits setExtraData made. That is a
    legitimate thing to do - the two styles are simply not meant to be mixed on
    one file - but it should never happen quietly.
*/
static void KV_CacheDrop(void)
{
    if (g_kvCacheValid && g_kvCacheDirty)
        LOG("MStorage kv: unsaved in-memory changes dropped - something else read the file\n");

    g_kvCacheValid = 0;
    g_kvCacheDirty = 0;
}

typedef struct KvLine
{
    int  start;      /* first byte of the line                                */
    int  end;        /* its newline, or the logical end for a last line       */
    char type;       /* 'i', 'f', 's', or 0 when the line is malformed        */
    int  valStart;   /* first byte of the value; == end when the value is ""  */
} KvLine;

/* What every member works out before it can touch the file. */
typedef struct KvCtx
{
    int   index;     /* card index, already decremented for the manager       */
    char* path;
    char* name;      /* NULL for the members that take a whole table          */
    int   nameLen;
    int   used;      /* content length now in g_kvFile                        */
    int   raw;       /* physical bytes the load saw, before the NUL clamp     */
    int   padTo;     /* blank up to here on commit - see KV_LoadFile          */
} KvCtx;

/*
    The receiver as a memory-card storage, or NULL.

    `this_` is the ADDRESS of the word holding the object - the same shape as an
    argv entry, which is why the game's own downcast helper at 0x3B63B8 opens
    with `lw $v1, ($a1)`. Comparing the vtable pointer replaces that helper: it
    is exact (nothing else in the image carries this value), and it rejects a
    hard-disk storage, which has no card index and would otherwise be read as
    one.
*/
static void* KV_Receiver(HObject* this_)
{
    char* self;

    if (this_ == NULL)
        return NULL;

    self = *(char**)this_;

    if (self == NULL || *(void**)(self + 4) != (void*)ADDR_mStorageMC_VTable)
        return NULL;

    return (void*)self;
}

static int KV_Equal(const char* a, const char* b, int n)
{
    int i;

    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;

    return 1;
}

/* Rejects every byte that could break a line - NUL, tab, newline, return and
   the rest of the control range - in one test. */
static int KV_Printable(const char* s, int len)
{
    int i;

    for (i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)s[i];

        if (c < 0x20 || c == 0x7F)
            return 0;
    }

    return 1;
}

static int KV_ValidName(const char* name, int len)
{
    if (len < 1 || len > KV_NAME_MAX)
        return 0;

    if (name[0] == '#')            /* reserved for the magic and for comments */
        return 0;

    return KV_Printable(name, len);
}

/*
    Read the whole file into g_kvFile.

    Returns the LOGICAL length (content up to the first NUL), or KV_ABSENT if
    the file is genuinely not there, or KV_UNUSABLE for anything else. The raw
    byte count lands in *rawOut, and the difference between the two decides
    whether setVar may create a file - see the comment there.

    Sized by over-reading rather than by MC_FileSizeBytes, deliberately.
    mcRead returns the ACTUAL count, not the requested length: the worker
    stashes sceMcRead's result at 0x5154CC and returns it at 0x5155D0 whenever
    the status is zero, and a short read is a success. MC_FileSizeBytes, by
    contrast, lists at most 25 directory entries and cannot tell "absent" from
    "not among the first 25" - and on that answer setVar would create a fresh
    file over an existing one. Reading is both cheaper and unambiguous.

    MC_FileSizeBytes stays correct for its own caller, which needs the size of
    the OLD file after new content is already on the card - something a read
    cannot answer.

    This calls the LIBRARY mcRead, not mStorageMC::read, so the terminating
    logic in HOOK__mStorageMC_read does not run and we terminate for ourselves.
*/
#define KV_ABSENT   (-1)
#define KV_UNUSABLE (-2)

static int KV_LoadFile(KvCtx* ctx)
{
    int rc, n, i;

    /* The read below overwrites the buffer the retained copy lives in. */
    KV_CacheDrop();

    rc = mcRead(ctx->index, ctx->path, g_kvFile, KV_FILE_MAX + 1, 0);

    ctx->raw   = rc;
    ctx->padTo = 0;

    /* -2 is "no such file" specifically, and no other failure shares it. The
       status mapper at 0x5145E8 turns the library's -4 (sceMcResNoEntry) into
       status 2, and the worker negates it at 0x5155C4. Every other failure -
       card removed, manager down, open or read error - arrives as -1, -3, -10
       or -11, and those must NOT be read as "absent": treating a transient read
       failure as an empty file would replace a good store with a one-line one. */
    if (rc == -2)
        return KV_ABSENT;

    if (rc < 0)
    {
        LOG("MStorage kv: read failed, rc=%d - refusing to write\n", rc);
        return KV_UNUSABLE;
    }

    /* Clamp at the first NUL. Required, not defensive: setVar writes a
       terminator after its content and the card never shrinks a file, so the
       physical file is routinely longer than what it holds. */
    n = rc;

    for (i = 0; i < n; i++)
    {
        if (g_kvFile[i] == 0)
        {
            n = i;
            break;
        }
    }

    /* Judge the size on the CONTENT, not the physical length. A file whose tail
       is old zero padding is perfectly editable, and testing the raw count here
       would brick it permanently - the card offers no way to shrink it back. */
    if (n > KV_FILE_MAX)
    {
        LOG("MStorage kv: over %d bytes of content, refusing to edit\n",
            KV_FILE_MAX);
        return KV_UNUSABLE;
    }

    /* How far a commit has to blank.

       Everything past the content is dead space the card cannot reclaim, and a
       write only needs to blank the part of it that still holds old text. The
       bytes are already in the buffer, so checking costs a scan and saves a
       card transaction per 64 bytes on every future write:

         tail already all zero -> blank only up to the OLD content end, which
                                  covers a rewrite that shrinks and nothing more
         tail holds anything   -> blank the whole thing once, and every write
                                  after this one takes the cheap path

       Without this, a file that once reached 3 KB and now holds 500 bytes pays
       forty extra mcWrites on every single write, permanently, re-zeroing bytes
       that were already zero. */
    ctx->padTo = n;

    for (i = n; i < rc; i++)
    {
        if (g_kvFile[i] != 0)
        {
            ctx->padTo = rc;
            break;
        }
    }

    g_kvFile[n] = 0;
    return n;
}

/*
    Find the line for `name`, matching on the NAME ALONE.

    A malformed line still matches, reported as type 0. setVar wants that - it
    should replace a corrupt line rather than append a second one beside it -
    and getVar checks the type itself.
*/
static int KV_Find(const char* buf, int n,
                   const char* name, int nameLen, KvLine* out)
{
    int p = 0;

    while (p < n)
    {
        int e = p;

        while (e < n && buf[e] != '\n')
            e++;

        if (e > p && buf[p] != '#')
        {
            int t = p;

            while (t < e && buf[t] != KV_SEP)
                t++;

            if (t < e && (t - p) == nameLen && KV_Equal(buf + p, name, nameLen))
            {
                out->start    = p;
                out->end      = e;
                out->type     = 0;
                out->valStart = e;

                /* Well formed needs <TAB> letter <TAB>, and room for them:
                   t + 2 < e puts the second tab at or before e - 1. */
                if (t + 2 < e && buf[t + 2] == KV_SEP)
                {
                    char ty = buf[t + 1];

                    if (ty == 'i' || ty == 'f' || ty == 's')
                    {
                        out->type     = ty;
                        out->valStart = t + 3;
                    }
                }

                return 1;
            }
        }

        p = (e < n) ? e + 1 : n;
    }

    return 0;
}

/*
    Replace `oldLen` bytes at `at` with `insLen` bytes of `ins`. Returns the new
    length, or -1 if it would not fit - checked BEFORE anything moves, so a
    refusal leaves the buffer untouched.
*/
static int KV_Splice(char* buf, int used, int at, int oldLen,
                     const char* ins, int insLen, int cap)
{
    int tail    = used - (at + oldLen);
    int newUsed = used - oldLen + insLen;
    int i;

    if (tail < 0 || newUsed > cap)
        return -1;

    if (insLen > oldLen)              /* growing: copy the tail back to front */
    {
        for (i = tail - 1; i >= 0; i--)
            buf[at + insLen + i] = buf[at + oldLen + i];
    }
    else if (insLen < oldLen)         /* shrinking: front to back             */
    {
        for (i = 0; i < tail; i++)
            buf[at + insLen + i] = buf[at + oldLen + i];
    }

    for (i = 0; i < insLen; i++)
        buf[at + i] = ins[i];

    buf[newUsed] = 0;                 /* buffer is cap + 2, always in range   */
    return newUsed;
}

/* Assemble name<TAB>type<TAB>value<NEWLINE>. No sprintf: every length here is
   already checked, and this build's printf has been caught inventing digits. */
static int KV_BuildLine(char* out, const char* name, int nameLen,
                        char type, const char* value, int valueLen)
{
    int i, k = 0;

    for (i = 0; i < nameLen; i++)
        out[k++] = name[i];

    out[k++] = KV_SEP;
    out[k++] = type;
    out[k++] = KV_SEP;

    for (i = 0; i < valueLen; i++)
        out[k++] = value[i];

    out[k++] = '\n';
    out[k]   = 0;

    return k;
}

/*
    Render a value to its stored text and report its type letter. Returns the
    text length, or -1 if the value cannot be stored.

    `out` needs KV_VALUE_MAX + 1 bytes - well over the 12 an int needs and the
    32 Adhoc_FormatFloat documents.
*/
static int KV_ValueText(hObject** slot, char* out, char* typeOut)
{
    char type = Adhoc_TypeLetter(slot);

    *typeOut = type;

    /* Adhoc_TypeLetter already answers in the three letters the file format
       uses, so there is no mapping step. */
    if (type == 'i')
    {
        _sprintf(out, "%d", Adhoc_ToInt(slot));
        return __strlen(out);
    }

    if (type == 'f')
    {
        if (!Adhoc_FormatFloat(out, Adhoc_ToFloat(slot)))
            LOG("MStorage kv: no decimal form survives the parser; storing raw bits\n");

        return __strlen(out);
    }

    if (type == 's')
    {
        char* s = Adhoc_CStr(slot);
        int   len;
        int   i;

        if (s == NULL)
            return -1;

        len = __strlen(s);

        if (len > KV_VALUE_MAX)
        {
            LOG("MStorage kv: string value is %d bytes, max %d\n",
                len, KV_VALUE_MAX);
            return -1;
        }

        if (!KV_Printable(s, len))
        {
            LOG("MStorage kv: string value has a control character; rejected\n");
            return -1;
        }

        /* An embedded NUL is invisible to __strlen, so ask the std::string for
           its real length - the _Rep header one word behind the data, the same
           field the stock write callback loads at 0x3B6DF0. Storing the prefix
           silently would be worse than refusing. */
        {
            char* data = *(char**)(*(char**)slot + 0x10);
            int   real = *(int*)(data - 0x10);

            if (real != len)
            {
                LOG("MStorage kv: string value contains a NUL; rejected\n");
                return -1;
            }
        }

        for (i = 0; i < len; i++)
            out[i] = s[i];

        out[len] = 0;
        return len;
    }

    LOG("MStorage kv: value is not an int, float or string\n");
    return -1;
}

/*
    Insert or replace one variable's line in the buffer. Returns the new content
    length, or -1 if it would not fit.

    The single definition of "write a variable" - setVar, setVars and
    setExtraData all come through here, so none of them can drift from the
    others about what replacing a line means.
*/
static int KV_Put(int used, const char* name, int nameLen,
                  char type, const char* value, int valueLen)
{
    char   line[KV_LINE_MAX + 1];
    KvLine hit;
    int    lineLen = KV_BuildLine(line, name, nameLen, type, value, valueLen);

    if (KV_Find(g_kvFile, used, name, nameLen, &hit))
    {
        /* Take the line's own newline with it when it has one. */
        int oldLen = (hit.end < used ? hit.end + 1 : hit.end) - hit.start;

        return KV_Splice(g_kvFile, used, hit.start, oldLen,
                         line, lineLen, KV_FILE_MAX);
    }

    return KV_Splice(g_kvFile, used, used, 0, line, lineLen, KV_FILE_MAX);
}

/*
    Build the adhoc value a found line holds into `dest`, which is the address
    of a word - a return slot or an array element, they are the same shape.
    Returns 1, or 0 if the line cannot be turned into a value.

    The single definition of "read a variable", shared by getVar, getVars and
    getExtraData.
*/
static int KV_ValueOf(const KvLine* hit, hObject** dest)
{
    char vtext[KV_VALUE_MAX + 1];
    int  vlen, i;

    if (hit->type == 0)
    {
        LOG("MStorage kv: the line for this name is malformed\n");
        return 0;
    }

    vlen = hit->end - hit->valStart;

    if (vlen > KV_VALUE_MAX)
    {
        LOG("MStorage kv: stored value is %d bytes, max %d\n", vlen, KV_VALUE_MAX);
        return 0;
    }

    for (i = 0; i < vlen; i++)
        vtext[i] = g_kvFile[hit->valStart + i];

    vtext[vlen] = 0;

    /* Tolerate a file hand-edited into CRLF. A return is a rejected byte on the
       way in, so this can never eat a legitimate one. */
    if (vlen > 0 && vtext[vlen - 1] == '\r')
        vtext[--vlen] = 0;

    if (hit->type == 'i')
    {
        HInt* r;
        HInt_HInt((HInt*)&r, Adhoc_ParseInt(vtext));
        Adhoc_ArrayStore(dest, (HObject*)r);
        HInt_dtor((HInt*)&r, 2);
    }
    else if (hit->type == 'f')
    {
        HFloat* r;
        ADHOC_MakeFloat(&r, Adhoc_ReadFloat(vtext));
        Adhoc_ArrayStore(dest, (HObject*)r);
        HFloat_dtor((HFloat*)&r, 2);
    }
    else
    {
        HString* r;
        Adhoc_MakeString(&r, vtext);
        Adhoc_ArrayStore(dest, (HObject*)r);
        HValue_dtor((void*)&r, 2);
    }

    return 1;
}

/*
    The receiver, the card index and argument 0, the path. No card traffic: the
    members that take a whole table validate it before spending a read.

    ctx->used starts at KV_UNUSABLE rather than KV_ABSENT on purpose. Those two
    look similar and behave oppositely - KV_Guard reads KV_ABSENT as "provably
    nothing to lose" and starts a fresh file over whatever is there. Any future
    path that resolves and guards without loading in between should therefore
    refuse, not create.
*/
static int KV_ResolvePath(HObject* this_, hObject** argv, KvCtx* ctx)
{
    void* self = KV_Receiver(this_);
    int   pathLen;

    if (self == NULL)
    {
        LOG("MStorage kv: this has to be a memory card storage\n");
        return 0;
    }

    ctx->index   = *(int*)((char*)self + MSTORAGEMC_INDEX_OFFSET) - 1;
    ctx->name    = NULL;
    ctx->nameLen = 0;
    ctx->used    = KV_UNUSABLE;
    ctx->raw     = 0;
    ctx->padTo   = 0;

    if (ctx->index < 0)
        return 0;

    if (Adhoc_TypeLetter(argv + 0) != 's')
    {
        LOG("MStorage kv: the path has to be a string\n");
        return 0;
    }

    ctx->path = Adhoc_CStr(argv + 0);
    if (ctx->path == NULL)
        return 0;

    /* Bounded before anything downstream copies it: MC_FileSizeBytes rejects a
       path of 128 or more, and several other places assume it fits. */
    pathLen = __strlen(ctx->path);
    if (pathLen < 1 || pathLen > 127)
    {
        LOG("MStorage kv: path is %d bytes, want 1 to 127\n", pathLen);
        return 0;
    }

    return 1;
}

/* Argument 1, the variable name. */
static int KV_ResolveName(hObject** argv, KvCtx* ctx)
{
    if (Adhoc_TypeLetter(argv + 1) != 's')
    {
        LOG("MStorage kv: the name has to be a string\n");
        return 0;
    }

    ctx->name = Adhoc_CStr(argv + 1);
    if (ctx->name == NULL)
        return 0;

    ctx->nameLen = __strlen(ctx->name);
    if (!KV_ValidName(ctx->name, ctx->nameLen))
    {
        LOG("MStorage kv: bad variable name, %d bytes\n", ctx->nameLen);
        return 0;
    }

    return 1;
}

/*
    Path, name and the file, for the single-value members.

    A missing file is NOT a failure here - ctx->used comes back as KV_ABSENT and
    the caller decides what that means, since getVar and setVar want opposite
    things from it.
*/
static int KV_Resolve(HObject* this_, hObject** argv, KvCtx* ctx)
{
    if (!KV_ResolvePath(this_, argv, ctx))
        return 0;

    if (!KV_ResolveName(argv, ctx))
        return 0;

    ctx->used = KV_LoadFile(ctx);

    return (ctx->used != KV_UNUSABLE);
}

/* Does the buffer hold one of ours? A trailing return is tolerated so a
   hand-edited CRLF file still passes. */
static int KV_HasMagic(int used)
{
    if (used < KV_MAGIC_LEN)
        return 0;

    if (!KV_Equal(g_kvFile, KV_MAGIC, KV_MAGIC_LEN - 1))
        return 0;

    return g_kvFile[KV_MAGIC_LEN - 1] == '\n'
        || g_kvFile[KV_MAGIC_LEN - 1] == '\r';
}

/*
    Leave g_kvFile holding a file this code is allowed to rewrite. Returns 1, or
    0 having refused. `create` starts a fresh one when there is nothing there.

    Everything that writes goes through here, because this is the guard: setVar
    rewrites a whole file, on the card that holds the player's saves, at a path
    a SCRIPT supplies. One mistyped path would otherwise overwrite a real save's
    front and zero the rest of it.
*/
static int KV_Guard(KvCtx* ctx, int create)
{
    /* Start fresh only when there is provably nothing to lose: the file is
       genuinely absent, or it is genuinely zero bytes long.

       Note this tests `raw`, not `used`. They differ for exactly one input, and
       it is the dangerous one: a binary save whose FIRST byte is NUL clamps to
       used == 0 while raw says thousands. Creating there would overwrite that
       save and pad the rest to zero - the precise accident this guard exists to
       prevent, reached around the back of it. */
    if (ctx->used == KV_ABSENT || ctx->raw == 0)
    {
        int i;

        if (!create)
            return 0;      /* nothing to edit, and making one is not our job */

        for (i = 0; i < KV_MAGIC_LEN; i++)
            g_kvFile[i] = KV_MAGIC[i];

        ctx->used = KV_MAGIC_LEN;
        g_kvFile[ctx->used] = 0;

        return 1;
    }

    if (!KV_HasMagic(ctx->used))
    {
        /* Deliberately permanent: there is no way to tell a file we merely have
           not seen before from one the game needs. Bootstrap an existing text
           file with storage.write(path, "#gt4kv 1\n") if you meant it. */
        LOG("MStorage kv: refusing to edit - this is not a %s file\n", "#gt4kv 1");
        return 0;
    }

    /* A hand-edited file may have lost its final newline. */
    if (g_kvFile[ctx->used - 1] != '\n')
    {
        if (ctx->used >= KV_FILE_MAX)
        {
            LOG("MStorage kv: no room to terminate the last line\n");
            return 0;
        }

        g_kvFile[ctx->used++] = '\n';
        g_kvFile[ctx->used]   = 0;
    }

    return 1;
}

/*
    Put `used` bytes of g_kvFile on the card, plus the terminator that follows
    them.

    That extra byte is what makes a shrinking rewrite safe, and it is worth
    being clear about why it is written here rather than left to
    HOOK__mStorageMC_write. The card cannot truncate, so a rewrite that shortens
    the file leaves the old tail in place. The hook exists to zero that tail,
    but it sizes the job with MC_FileSizeBytes, which gives up after 25
    directory entries and on a path of 128 or more - and when it gives up it
    pads nothing and still reports success. A store on a busy card would then
    keep resurrecting removed lines, each rewrite promoting the stale tail back
    into content.

    An in-file terminator depends on none of that. The next load stops exactly
    where this content stops, however the tail is left.
*/
static int KV_Commit(KvCtx* ctx, int used)
{
    int rc;

    if (used < 0)
    {
        LOG("MStorage kv: file would pass %d bytes; nothing written\n",
            KV_FILE_MAX);
        return 0;
    }

    rc = mcWrite(ctx->index, ctx->path, g_kvFile, used + 1, 0);

    if (rc != 0)
    {
        /* The directory is the usual cause: mStorageMC::mkdir is a stub and
           nothing on this side ever passes sceMcFileCreateDir, so the parent
           must already exist. The FILE is created for us - the write worker
           retries the open with sceMcFileCreateFile at 0x514A78 when the first
           attempt returns -4 and the offset is zero. */
        LOG("MStorage kv: write failed, rc=%d - does the directory exist?\n", rc);
        return 0;
    }

    /* Best effort tidying, not correctness: blank whatever of the old file
       still sits past the terminator, so a raw dump of the card is not littered
       with what used to be there. Failure is ignored on purpose - the content
       and its terminator are already safely down.

       Bounded by padTo rather than by the physical size, so this costs nothing
       once the tail is clean. See KV_LoadFile: a file whose dead space is
       already zero only needs the region the old CONTENT occupied blanked, and
       usually not even that. Without it every write re-zeroes the file's
       all-time high-water mark, 64 bytes per card transaction, forever. */
    if (ctx->padTo > used + 1)
    {
        static const char zeros[64] = {0};

        int at        = used + 1;
        int remaining = ctx->padTo - at;

        while (remaining > 0)
        {
            int chunk = remaining < (int)sizeof(zeros)
                      ? remaining : (int)sizeof(zeros);

            if (mcWrite(ctx->index, ctx->path, zeros, chunk, at) != 0)
                break;

            at        += chunk;
            remaining -= chunk;
        }
    }

    return 1;
}

static int KV_SetVar(HObject* this_, hObject** argv)
{
    KvCtx ctx;
    char  vtext[KV_VALUE_MAX + 1];
    char  type;
    int   vlen, used;

    if (!KV_Resolve(this_, argv, &ctx))
        return 0;

    /* Before the guard, so an unstorable value is reported without the file
       having been judged - and so a fresh file is not created for a write that
       was never going to happen. */
    vlen = KV_ValueText(argv + 2, vtext, &type);
    if (vlen < 0)
        return 0;

    if (!KV_Guard(&ctx, 1))
        return 0;

    used = KV_Put(ctx.used, ctx.name, ctx.nameLen, type, vtext, vlen);

    return KV_Commit(&ctx, used);
}

/*
    Remove a variable. Returns 1 if a line went away, 0 if there was nothing to
    remove or the file could not be edited.

    Pure shrink, which makes it the sharpest test of the terminator in
    KV_Commit: the removed line's bytes stay on the card afterwards, and only
    that terminator stops the next load from reading them back as content.
*/
static int KV_DelVar(HObject* this_, hObject** argv)
{
    KvCtx  ctx;
    KvLine hit;
    int    used, oldLen;

    if (!KV_Resolve(this_, argv, &ctx))
        return 0;

    /* No creating: a file that is not there has nothing to remove. */
    if (!KV_Guard(&ctx, 0))
        return 0;

    used = ctx.used;

    if (!KV_Find(g_kvFile, used, ctx.name, ctx.nameLen, &hit))
        return 0;

    oldLen = (hit.end < used ? hit.end + 1 : hit.end) - hit.start;
    used   = KV_Splice(g_kvFile, used, hit.start, oldLen, "", 0, KV_FILE_MAX);

    return KV_Commit(&ctx, used);
}

/* MStorage::setVar(path, name, value) -> 1 on success, 0 on any failure. */
void m_setVar(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    HInt* result;
    int   status = 0;

    /* Nothing in the VM checks arity - hBuiltinMethod carries no argument count
       - so reading argv[2] with argc < 3 would pick up a live object from the
       operand stack. */
    if (argc < 3)
    {
        LOG("MStorage kv: setVar takes (path, name, value)\n");
    }
    else if (g_kvBusy)
    {
        LOG("MStorage kv: setVar re-entered; refused\n");
    }
    else
    {
        g_kvBusy = 1;
        status   = KV_SetVar(this_, argv);
        g_kvBusy = 0;
    }

    /* Always an int, including on the argument-count path, so one test covers
       every failure. Stock natives leave the slot untouched instead, which
       would make a script check two different things. */
    HInt_HInt((HInt*)&result, status);
    ADHOC_MakeRef(return_value, (HObject*)result);
    HInt_dtor((HInt*)&result, 2);
}

/*
    MStorage::delVar(path, name) -> 1 if the variable was removed, 0 otherwise.

    A separate member rather than a two-argument setVar on purpose: the card has
    no delete of its own, so this is the only way to take something back out,
    and it should not be reachable by miscounting the arguments to setVar.
*/
void m_delVar(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    HInt* result;
    int   status = 0;

    if (argc < 2)
    {
        LOG("MStorage kv: delVar takes (path, name)\n");
    }
    else if (g_kvBusy)
    {
        LOG("MStorage kv: delVar re-entered; refused\n");
    }
    else
    {
        g_kvBusy = 1;
        status   = KV_DelVar(this_, argv);
        g_kvBusy = 0;
    }

    HInt_HInt((HInt*)&result, status);
    ADHOC_MakeRef(return_value, (HObject*)result);
    HInt_dtor((HInt*)&result, 2);
}

/* ---- whole tables ---------------------------------------------------------

   setVar and getVar each cost a full card read and a full card write, because
   each one loads the file, edits one line and writes it back. Twenty variables
   is forty card transactions, and a card pulled partway through leaves some
   variables saved and the rest not.

   setVars and getVars take the whole table at once - an adhoc array of
   [name, value] pairs - and do the same work with one read and one write. The
   bytes written are identical to calling setVar in a loop; only the card
   traffic changes. And because a table is now written in a single operation,
   it lands or it does not, rather than tearing halfway down the list.
*/

/* A sanity bound, not a capacity bound - the real limit is the 4 KB file, which
   KV_Splice enforces per row. This exists so that a corrupt or hostile array
   cannot make the menu thread walk for minutes. */
#define KV_MAX_VARS 64

/* getVars could not read the file at all, as distinct from reading it and
   finding nothing. The two must not be confused: see KV_GetVars. */
#define KV_GET_UNUSABLE (-1)

/*
    One row of a table, validated: an array of EXACTLY two elements whose first
    is a string that passes KV_ValidName. Fills the outputs and returns 1, or
    returns 0 having touched none of them.

    Exactly two, not at least two. A third column means the table is not the
    shape this API documents, and quietly taking the first two would hide the
    mistake instead of reporting it.

    Every field is checked before the next is dereferenced: the row slot, the
    object behind it, its vtable, its element count, element 0's type, its text,
    then the name's own bytes.
*/
static int KV_RowFields(hObject** table, int i,
                        hObject*** valueOut, char** nameOut, int* lenOut)
{
    hObject** row = Adhoc_ArrayElem(table, i);
    hObject** nameSlot;
    hObject** valueSlot;
    char*     name;
    int       nameLen;

    if (row == NULL || !Adhoc_IsArray(row) || Adhoc_ArrayCount(row) != 2)
        return 0;

    nameSlot  = Adhoc_ArrayElem(row, 0);
    valueSlot = Adhoc_ArrayElem(row, 1);

    if (nameSlot == NULL || valueSlot == NULL)
        return 0;

    if (Adhoc_TypeLetter(nameSlot) != 's')
        return 0;

    name = Adhoc_CStr(nameSlot);
    if (name == NULL)
        return 0;

    nameLen = __strlen(name);
    if (!KV_ValidName(name, nameLen))
        return 0;

    /* Set together, on the success path only, so a caller that ignores the
       return value cannot pick up one initialised output and one stale one. */
    *valueOut = valueSlot;
    *nameOut  = name;
    *lenOut   = nameLen;

    return 1;
}

/* Does an earlier row carry this name too?

   Worth the nested walk: two rows with one name would splice into a single
   line, the later winning, and the earlier variable would vanish from the
   player's save with nothing said. Every other malformed row is refused
   loudly, so this one is too. At most KV_MAX_VARS short strings. */
static int KV_EarlierRowNamed(hObject** table, int upto,
                              const char* name, int nameLen)
{
    int j;

    for (j = 0; j < upto; j++)
    {
        hObject** valueSlot;
        char*     other;
        int       otherLen;

        if (!KV_RowFields(table, j, &valueSlot, &other, &otherLen))
            continue;

        if (otherLen == nameLen && KV_Equal(other, name, nameLen))
            return 1;
    }

    return 0;
}

/*
    storage.setVars(path, [[name, value], ...]) -> 1 or 0

    ALL OR NOTHING, and that comes free rather than being engineered. KV_Commit
    is the only thing here that touches the card, so giving up at a bad row
    leaves the file byte for byte as it was - there is no such thing as a
    half-written batch. Leaving the buffer half-spliced costs nothing either,
    since the next call reloads it.

    Writing the good rows and skipping the bad was the alternative, and it is
    worse: a script cannot learn WHICH rows landed, so it would hold a save
    whose contents it cannot predict from the return value, and the failure mode
    is a variable quietly missing from the player's file forever. Refusing with
    the row index in the log is diagnosable and idempotent - fix the row, call
    again - and it keeps the contract identical to setVar's.
*/
static int KV_SetVars(HObject* this_, hObject** argv)
{
    KvCtx       ctx;
    char        vtext[KV_VALUE_MAX + 1];
    int         rows, i, used;
    int         badRow = -1;
    const char* badWhy = "";

    if (!KV_ResolvePath(this_, argv, &ctx))
        return 0;

    if (!Adhoc_IsArray(argv + 1))
    {
        LOG("MStorage kv: setVars wants a table - [[name, value], ...]\n");
        return 0;
    }

    rows = Adhoc_ArrayCount(argv + 1);

    if (rows < 1 || rows > KV_MAX_VARS)
    {
        LOG("MStorage kv: setVars got %d rows, want 1 to %d\n", rows, KV_MAX_VARS);
        return 0;
    }

    /* Only now is a card read worth spending. */
    ctx.used = KV_LoadFile(&ctx);

    if (ctx.used == KV_UNUSABLE)
        return 0;

    /* Guarded before the rows are inspected, where setVar checks its one value
       first. The inversion is safe only because KV_Commit is the sole card
       write and runs after the loop, so a table that turns out to be bad
       creates nothing. Anyone moving the commit earlier must revisit this. */
    if (!KV_Guard(&ctx, 1))
        return 0;

    used = ctx.used;

    for (i = 0; i < rows; i++)
    {
        hObject** valueSlot;
        char*     name;
        char      type;
        int       nameLen, vlen;

        if (!KV_RowFields(argv + 1, i, &valueSlot, &name, &nameLen))
        {
            badRow = i;
            badWhy = "is not a [name, value] pair";
            break;
        }

        if (KV_EarlierRowNamed(argv + 1, i, name, nameLen))
        {
            badRow = i;
            badWhy = "repeats a name an earlier row already used";
            break;
        }

        /* KV_ValueText carries the whole type policy and logs its own reason:
           int, float or string only, and a string bounded, printable and free
           of embedded NULs. */
        vlen = KV_ValueText(valueSlot, vtext, &type);

        if (vlen < 0)
        {
            badRow = i;
            badWhy = "holds a value that cannot be stored";
            break;
        }

        used = KV_Put(used, name, nameLen, type, vtext, vlen);

        if (used < 0)
        {
            badRow = i;
            badWhy = "would push the file past its size limit";
            break;
        }
    }

    /* Reported out here rather than inside the loop: LOG declares a 256-byte
       buffer at every expansion, and this runs on a menu thread at an unbounded
       interpreter depth. */
    if (badRow >= 0)
    {
        LOG("MStorage kv: setVars row %d %s; nothing written\n", badRow, badWhy);
        return 0;
    }

    return KV_Commit(&ctx, used);
}

/*
    storage.getVars(path, [[name, value], ...]) -> rows filled, or -1

    Fills each row's second element from the file, in place. A row the file has
    no entry for keeps the value the script put there - the same fallback rule
    getVar has, applied per row and for free.

    PER-ROW TOLERANT where setVars is all or nothing, and the asymmetry is
    deliberate. For a read there is a well-defined safe outcome for a row we
    cannot use - leave it exactly as it is - and that outcome is already part of
    the contract. For a write there is no equivalent.

    UNLIKE getVar, this DOES check the magic line, because getVar's reason for
    skipping it does not survive here. getVar only produced a value the script
    was free to ignore; getVars overwrites the script's own live table, so a
    stale or mistyped path would silently replace the player's values with
    whatever a foreign file's matching lines happened to say. A read that
    mutates needs the same guard a write does.

    Returns -1, not 0, when the card cannot be read or the file is not ours. A
    script must tell those apart from "nothing stored yet": taking a transient
    card failure for an empty file and then saving would write freshly
    defaulted values over a perfectly good save. Note -1 is TRUTHY in adhoc, so
    the test is `n < 0`, never `!n`.

    This is a MERGE rather than a load, which matters on the second call: a row
    the file does not mention keeps whatever is in it NOW, which after an
    earlier getVars may be a value from a different file. Load one table from
    one path.
*/
static int KV_GetVars(HObject* this_, hObject** argv)
{
    KvCtx  ctx;
    char*  table;
    int    rows, i, filled = 0;
    int    badRow = -1;

    if (!KV_ResolvePath(this_, argv, &ctx))
        return KV_GET_UNUSABLE;

    if (!Adhoc_IsArray(argv + 1))
    {
        LOG("MStorage kv: getVars wants a table - [[name, value], ...]\n");
        return KV_GET_UNUSABLE;
    }

    rows = Adhoc_ArrayCount(argv + 1);

    if (rows < 1 || rows > KV_MAX_VARS)
    {
        LOG("MStorage kv: getVars got %d rows, want 1 to %d\n", rows, KV_MAX_VARS);
        return KV_GET_UNUSABLE;
    }

    ctx.used = KV_LoadFile(&ctx);

    if (ctx.used == KV_UNUSABLE)
        return KV_GET_UNUSABLE;

    /* Nothing stored yet is not a failure - every row keeps its own value,
       which is exactly the fallback the caller supplied by filling the table. */
    if (ctx.used == KV_ABSENT || ctx.used == 0)
        return 0;

    if (!KV_HasMagic(ctx.used))
    {
        LOG("MStorage kv: getVars refused - this is not a %s file\n", "#gt4kv 1");
        return KV_GET_UNUSABLE;
    }

    /* Hold a reference on the table for the length of the walk.

       Replacing a row's value releases whatever was there, and a release that
       reaches zero destroys the object through its vtable. If a script built a
       table that referred to itself, that release could free the very array
       being walked. One reference removes the question, and it also means we do
       not depend on argv[1]'s operand-stack slot still counting as a live
       reference - mCall has already decremented the stack top by the time our
       callback runs. */
    table = *(char**)(argv + 1);
    RefCounter_ref((RefCounter*)table);

    for (i = 0; i < rows; i++)
    {
        hObject** valueSlot;
        KvLine    hit;
        char*     name;
        int       nameLen;

        if (!KV_RowFields(argv + 1, i, &valueSlot, &name, &nameLen))
        {
            badRow = i;
            continue;
        }

        if (!KV_Find(g_kvFile, ctx.used, name, nameLen, &hit))
            continue;                 /* not stored: the row's own value stands */

        /* KV_ValueOf builds the value and stores it into the row, refing it and
           releasing the row's previous occupant. */
        if (!KV_ValueOf(&hit, valueSlot))
        {
            badRow = i;
            continue;
        }

        filled++;
    }

    RefCounter_unref((RefCounter*)table);

    if (badRow >= 0)
        LOG("MStorage kv: getVars left row %d as it was; check the table\n", badRow);

    return filled;
}

/* MStorage::setVars(path, table) -> 1 if the whole table was written. */
void m_setVars(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    HInt* result;
    int   status = 0;

    /* Nothing in the VM checks arity, so reading argv[1] with argc < 2 would
       take a live object off the operand stack and treat it as an array. */
    if (argc < 2)
    {
        LOG("MStorage kv: setVars takes (path, table)\n");
    }
    else if (g_kvBusy)
    {
        LOG("MStorage kv: setVars re-entered; refused\n");
    }
    else
    {
        g_kvBusy = 1;
        status   = KV_SetVars(this_, argv);
        g_kvBusy = 0;
    }

    HInt_HInt((HInt*)&result, status);
    ADHOC_MakeRef(return_value, (HObject*)result);
    HInt_dtor((HInt*)&result, 2);
}

/* MStorage::getVars(path, table) -> rows filled, or -1 if nothing was read. */
void m_getVars(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    HInt* result;
    int   filled = KV_GET_UNUSABLE;

    if (argc < 2)
    {
        LOG("MStorage kv: getVars takes (path, table)\n");
    }
    else if (g_kvBusy)
    {
        LOG("MStorage kv: getVars re-entered; refused\n");
    }
    else
    {
        g_kvBusy = 1;
        filled   = KV_GetVars(this_, argv);
        g_kvBusy = 0;
    }

    HInt_HInt((HInt*)&result, filled);
    ADHOC_MakeRef(return_value, (HObject*)result);
    HInt_dtor((HInt*)&result, 2);
}

/* Fills return_value and returns 1, or returns 0 having touched nothing. */
static int KV_GetVar(HObject* return_value, HObject* this_, hObject** argv)
{
    KvCtx  ctx;
    KvLine hit;

    if (!KV_Resolve(this_, argv, &ctx))
        return 0;

    if (ctx.used <= 0)
        return 0;              /* absent, or empty */

    /* No KV_Guard here on purpose. Reading harms nothing, so getVar stays
       usable against any text file - and refusing would only mean the magic
       line had to be explained twice. setVar carries the whole of the
       protection because setVar is the only thing that can destroy anything. */

    if (!KV_Find(g_kvFile, ctx.used, ctx.name, ctx.nameLen, &hit))
        return 0;

    /* The return slot is the address of a word, the same shape as an array
       element, so the one value builder serves both. */
    return KV_ValueOf(&hit, (hObject**)return_value);
}

/*
    MStorage::getVar(path, name [, fallback])

    Returns the stored value with the type it was stored as. When the variable
    is not there - or the file is not, or the card is unreadable - it hands back
    the fallback if one was given, and nil otherwise.
*/
void m_getVar(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    int done = 0;

    if (argc < 2)
    {
        LOG("MStorage kv: getVar takes (path, name [, fallback])\n");
    }
    else if (g_kvBusy)
    {
        LOG("MStorage kv: getVar re-entered; refused\n");
    }
    else
    {
        g_kvBusy = 1;
        done     = KV_GetVar(return_value, this_, argv);
        g_kvBusy = 0;
    }

    if (done)
        return;

    /* The caller's own fallback needs no construction - a ref and a store. */
    if (argc >= 3)
    {
        ADHOC_MakeRef(return_value, (HObject*)argv[2]);
        return;
    }

    /* A real hNil rather than the null handle the slot already holds. Both read
       as nil in a comparison, but only this one survives `if (v)`: mJumpZero
       loads the object's vtable at 0x5006E8 with no null check and calls
       through it. */
    {
        HObject* nil;
        HNil_HNil((HObject*)&nil);
        ADHOC_MakeRef(return_value, nil);
        HValue_dtor((void*)&nil, 2);
    }
}

/* ---- the in-memory store --------------------------------------------------

   Four members that separate WHEN the card is touched from WHEN a value is
   read or written:

     storage.loadVars(path)            card -> memory,  once, in a menu
     MStorage::getExtraData(name, ...) memory only,     anywhere, any state
     MStorage::setExtraData(name, v)   memory only,     anywhere, any state
     storage.saveVars(path)            memory -> card,  once, on a save

   getExtraData and setExtraData are FUNCTIONS rather than methods on purpose.
   A method needs a receiver, which means getStorage() first, and a race-context
   script may not be able to build one - while these touch no card and have no
   use for a storage object at all. Call them fully qualified:

     main::menu::MStorage::setExtraData("laps", 5);
     var n = main::menu::MStorage::getExtraData("laps", 0);
*/

/* Variable lines only - the magic and any comment line are not counted. */
static int KV_CountVars(int used)
{
    int p = 0;
    int n = 0;

    while (p < used)
    {
        int e = p;

        while (e < used && g_kvFile[e] != '\n')
            e++;

        if (e > p && g_kvFile[p] != '#')
            n++;

        p = (e < used) ? e + 1 : used;
    }

    return n;
}

/*
    storage.loadVars(path) -> variables loaded, or -1

    A file that is not there is NOT a failure: the store simply starts empty and
    the first saveVars creates it. A file that is not ours IS a failure, because
    saveVars would later write over it - this is the only place that check can
    be made, since saveVars deliberately does not read before writing.
*/
static int KV_LoadVars(HObject* this_, hObject** argv)
{
    KvCtx ctx;
    int   pathLen, i;

    if (!KV_ResolvePath(this_, argv, &ctx))
        return -1;

    ctx.used = KV_LoadFile(&ctx);

    if (ctx.used == KV_UNUSABLE)
        return -1;

    if (ctx.used == KV_ABSENT || ctx.used == 0)
    {
        for (i = 0; i < KV_MAGIC_LEN; i++)
            g_kvFile[i] = KV_MAGIC[i];

        ctx.used  = KV_MAGIC_LEN;
        ctx.raw   = 0;
        ctx.padTo = 0;

        g_kvFile[ctx.used] = 0;
    }
    else if (!KV_HasMagic(ctx.used))
    {
        LOG("MStorage kv: loadVars refused - this is not a %s file\n", "#gt4kv 1");
        return -1;
    }

    /* KV_ResolvePath has already bounded this at 127. */
    pathLen = __strlen(ctx.path);

    for (i = 0; i <= pathLen; i++)
        g_kvCachePath[i] = ctx.path[i];

    g_kvCacheUsed  = ctx.used;
    g_kvCacheRaw   = ctx.raw;
    g_kvCachePadTo = ctx.padTo;
    g_kvCacheValid = 1;
    g_kvCacheDirty = 0;

    return KV_CountVars(ctx.used);
}

/*
    storage.saveVars(path) -> 1 or 0

    One write, straight from memory, with no read first.

    The path has to be the one loadVars read. That is the whole of the safety
    here: the store was checked against the magic line when it was loaded, and
    since this never reads the target it has no other way to know whether it is
    about to land on somebody else's file.
*/
static int KV_SaveVars(HObject* this_, hObject** argv)
{
    KvCtx ctx;

    if (!KV_ResolvePath(this_, argv, &ctx))
        return 0;

    if (!g_kvCacheValid)
    {
        LOG("MStorage kv: saveVars has nothing in memory - call loadVars first\n");
        return 0;
    }

    if (__strcmp(ctx.path, g_kvCachePath) != 0)
    {
        LOG("MStorage kv: saveVars path is not the one loadVars read\n");
        return 0;
    }

    ctx.used  = g_kvCacheUsed;
    ctx.raw   = g_kvCacheRaw;
    ctx.padTo = g_kvCachePadTo;

    if (!KV_Commit(&ctx, g_kvCacheUsed))
        return 0;

    /* The card now matches memory, and everything past the terminator we just
       wrote is blank - so the next save has nothing to tidy. */
    g_kvCacheDirty = 0;
    g_kvCacheRaw   = g_kvCacheUsed + 1;
    g_kvCachePadTo = g_kvCacheUsed + 1;

    return 1;
}

static int KV_SetExtraData(hObject** argv)
{
    char  vtext[KV_VALUE_MAX + 1];
    char  type;
    char* name;
    int   nameLen, vlen, used;

    if (Adhoc_TypeLetter(argv + 0) != 's')
    {
        LOG("MStorage kv: setExtraData wants a name string\n");
        return 0;
    }

    name = Adhoc_CStr(argv + 0);
    if (name == NULL)
        return 0;

    nameLen = __strlen(name);
    if (!KV_ValidName(name, nameLen))
    {
        LOG("MStorage kv: bad variable name, %d bytes\n", nameLen);
        return 0;
    }

    vlen = KV_ValueText(argv + 1, vtext, &type);
    if (vlen < 0)
        return 0;

    used = KV_Put(g_kvCacheUsed, name, nameLen, type, vtext, vlen);

    if (used < 0)
    {
        LOG("MStorage kv: setExtraData would pass %d bytes\n", KV_FILE_MAX);
        return 0;
    }

    g_kvCacheUsed  = used;
    g_kvCacheDirty = 1;

    return 1;
}

/* MStorage::loadVars(path) -> variables loaded, or -1. */
void m_loadVars(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    HInt* result;
    int   loaded = -1;

    if (argc < 1)
    {
        LOG("MStorage kv: loadVars takes (path)\n");
    }
    else if (g_kvBusy)
    {
        LOG("MStorage kv: loadVars re-entered; refused\n");
    }
    else
    {
        g_kvBusy = 1;
        loaded   = KV_LoadVars(this_, argv);
        g_kvBusy = 0;
    }

    HInt_HInt((HInt*)&result, loaded);
    ADHOC_MakeRef(return_value, (HObject*)result);
    HInt_dtor((HInt*)&result, 2);
}

/* MStorage::saveVars(path) -> 1 or 0. */
void m_saveVars(HObject* return_value, HObject* this_, int argc, hObject** argv)
{
    HInt* result;
    int   status = 0;

    if (argc < 1)
    {
        LOG("MStorage kv: saveVars takes (path)\n");
    }
    else if (g_kvBusy)
    {
        LOG("MStorage kv: saveVars re-entered; refused\n");
    }
    else
    {
        g_kvBusy = 1;
        status   = KV_SaveVars(this_, argv);
        g_kvBusy = 0;
    }

    HInt_HInt((HInt*)&result, status);
    ADHOC_MakeRef(return_value, (HObject*)result);
    HInt_dtor((HInt*)&result, 2);
}

/*
    MStorage::getExtraData(name [, fallback])

    The stored value with the type it was stored as; the fallback when the name
    is not in the store; nil when no fallback was given. Memory only.
*/
void f_getExtraData(HObject* return_value, int argc, hObject** argv)
{
    KvLine hit;
    int    done = 0;

    if (argc < 1)
    {
        LOG("MStorage kv: getExtraData takes (name [, fallback])\n");
    }
    else if (!g_kvCacheValid)
    {
        LOG("MStorage kv: getExtraData has nothing in memory - call loadVars first\n");
    }
    else if (g_kvBusy)
    {
        LOG("MStorage kv: getExtraData re-entered; refused\n");
    }
    else if (Adhoc_TypeLetter(argv + 0) != 's')
    {
        LOG("MStorage kv: getExtraData wants a name string\n");
    }
    else
    {
        char* name = Adhoc_CStr(argv + 0);

        if (name != NULL)
        {
            int nameLen = __strlen(name);

            g_kvBusy = 1;

            if (KV_ValidName(name, nameLen) &&
                KV_Find(g_kvFile, g_kvCacheUsed, name, nameLen, &hit))
            {
                done = KV_ValueOf(&hit, (hObject**)return_value);
            }

            g_kvBusy = 0;
        }
    }

    if (done)
        return;

    /* The caller's own fallback needs no construction - a ref and a store. */
    if (argc >= 2)
    {
        ADHOC_MakeRef(return_value, (HObject*)argv[1]);
        return;
    }

    /* A real hNil rather than the null handle the slot already holds, so
       `if (v)` is safe on it. See ADDR_HNil_HNil. */
    {
        HObject* nil;
        HNil_HNil((HObject*)&nil);
        ADHOC_MakeRef(return_value, nil);
        HValue_dtor((void*)&nil, 2);
    }
}

/*
    MStorage::setExtraData(name, value) -> 1 or 0

    Edits the in-memory store and nothing else. Nothing reaches the card until
    saveVars, which is the point: this is callable mid-race, as often as you
    like, at no card cost.
*/
void f_setExtraData(HObject* return_value, int argc, hObject** argv)
{
    HInt* result;
    int   status = 0;

    if (argc < 2)
    {
        LOG("MStorage kv: setExtraData takes (name, value)\n");
    }
    else if (!g_kvCacheValid)
    {
        LOG("MStorage kv: setExtraData has nothing in memory - call loadVars first\n");
    }
    else if (g_kvBusy)
    {
        LOG("MStorage kv: setExtraData re-entered; refused\n");
    }
    else
    {
        g_kvBusy = 1;
        status   = KV_SetExtraData(argv);
        g_kvBusy = 0;
    }

    HInt_HInt((HInt*)&result, status);
    ADHOC_MakeRef(return_value, (HObject*)result);
    HInt_dtor((HInt*)&result, 2);
}
#endif

#if MSTORAGE_WRITE_TRUNCATES
/*
    Make a shorter write replace the file instead of overwriting its front.

    The card has no truncate and no delete - mStorageMC has neither, and neither
    does the manager beneath it - so write puts its bytes at offset 0 and leaves
    everything past them alone. Writing "test" over "write test" left
    "teste test".

    So pad instead: after the real write, fill the rest of the OLD file with
    zero bytes. Because the adhoc read builds its string with strlen(), a zero
    at the new end makes the value a script sees stop exactly where the new
    content does. The file keeps its old length on the card - it never shrinks
    physically - but it reads as though it had been truncated.

    This only ever pads. It never shortens, never deletes, and does nothing at
    all when the new content is the same length or longer, which is why
    mOption::save - which comes through this same vtable slot with a constant
    rounded length - is unaffected.
*/
int HOOK__mStorageMC_write(void* this, const char* name, const char* buf, int len)
{
    int index = *(int*)((char*)this + MSTORAGEMC_INDEX_OFFSET) - 1;
    if (index < 0)
        return -1;

    /* The real size, NOT the getFileSize hook - that one adds a byte for the
       read terminator, and treating it as a length here would grow the file by
       one on every single write. */
    int oldSize = MC_FileSizeBytes(this, name);

    int rc = mcWrite(index, name, buf, len, 0);

    /* mcWrite returns a status, not a count: 0 is success. Preserve the thunk's
       own convention - the requested length on success, -error otherwise. */
    if (rc != 0)
        return -rc;

    if (oldSize > len)
    {
        /* Static, not a local: this runs on the adhoc thread and the frame
           already carries the directory table. */
        static const char zeros[64] = {0};

        int remaining = oldSize - len;
        int at = len;

        while (remaining > 0)
        {
            int chunk = remaining < (int)sizeof(zeros) ? remaining : (int)sizeof(zeros);

            if (mcWrite(index, name, zeros, chunk, at) != 0)
                break;   /* the content is already correct; the tail is cosmetic */

            at += chunk;
            remaining -= chunk;
        }

        LOG("mStorageMC::write: padded %d stale bytes after %d\n", oldSize - len, len);
    }

    return len;
}
#endif

/*
    Terminate what read() hands back.

    The adhoc read callback sizes its buffer from getFileSize, reads that many
    bytes into it, and then builds its string from strlen() of the result.
    Nothing zeroes the buffer and the card supplies no terminator, so on its own
    the string runs past the end of the file into whatever heap bytes follow.

    getFileSize therefore reports one byte MORE than the file holds, and this
    terminates whatever space is left over. The read itself is NOT shortened,
    because this is not the only caller: mOption::load also comes through here
    with a buffer sized exactly to its record, and it gets rc == len and is left
    alone. Only a caller that asked for more than the file holds - which is the
    adhoc path, thanks to the + 1 - gets a terminator.

    That keeps the terminator out of the file, so a plain text file works and a
    write()/read() round trip returns exactly what was written.
*/
int HOOK__mStorageMC_read(void* this, const char* name, char* buf, int len)
{
    if (buf == NULL || len <= 0)
        return -1;

    int index = *(int*)((char*)this + MSTORAGEMC_INDEX_OFFSET) - 1;
    if (index < 0)
    {
        buf[0] = 0;
        return -1;
    }

    /* Ask for everything the caller allocated - do NOT shorten the read.

       We are not the only caller. mOption::load (adhoc `load`, via 0x161B98 ->
       0x163CC0 -> 0x163B48) also comes through here, and it sizes its buffer
       itself: (recordSize + 0x3F) & ~0x3F, rounded up to 64, with no room to
       spare. Reading len - 1 there dropped the last byte of every option record
       loaded from a card, and mOption::save writes that same rounded length, so
       a save/load round trip lost a byte. */
    int rc = mcRead(index, name, buf, len, 0);

    /* rc is the BYTE COUNT on success and a negative error otherwise - the
       worker converts internally (0x5155C4 negu / 0x5155D0 movz: return the
       count when the status is 0, else -status). Note this is NOT the same
       convention as write, whose worker hands back a status that
       mStorageMC::write converts for itself. */
    if (rc < 0)
    {
        buf[0] = 0;
        return rc;
    }

    /* Terminate only when the read came up short, which is exactly when there
       is a spare byte to put it in. The adhoc path gets one because
       getFileSize reports size + 1, so a size-byte file returns rc = size and
       buf[size] is ours to write. A caller that filled its buffer completely
       (mOption) gets rc == len and is left untouched - writing buf[len] there
       would run one byte past its allocation. */
    if (rc < len)
        buf[rc] = 0;

    return rc;
}

/*
    The true size of a file on the card, in bytes, or -1 if it is not there.

    Split out of the getFileSize hook on purpose. That hook returns one byte
    MORE than the file holds, deliberately, so the adhoc read path has room for
    a terminator - which makes it the wrong thing for any other code to call.
    Anything inside the plugin that wants a real byte count calls this.
*/
static int MC_FileSizeBytes(void* this, const char* name)
{
    /* The manager is reached THROUGH the pointer at ADDR_MC_Manager_Ptr, and it
       is null until the game builds it. Locking the address of the pointer
       instead of what it points at waits on unrelated memory and hangs. */
    /* Nothing below logs `name` unguarded. LOG formats with the game's sprintf
       into a 256-byte local and does not bound anything, so a long caller path
       would run off the end of this frame and over the saved return address. */
    void* manager = *(void**)ADDR_MC_Manager_Ptr;
    if (manager == NULL)
    {
        LOG("mStorageMC: memory card manager not up yet\n");
        return -1;
    }

    /* No lock here. The manager monitor guards manager state this function never
       touches, and mcGetDir takes the libmc monitor (0x689458) itself - that is
       the lock the game actually holds around a directory listing. */

    /* The index is one combined value; the manager splits it into port and slot
       at 0x514A40 / 0x514A4C. Doing that here keeps getStorage(2) - which is
       port 0 slot 1, not port 1 - reading the card the caller asked for. */
    int index = *(int*)((char*)this + MSTORAGEMC_INDEX_OFFSET) - 1;
    if (index < 0)
        return -1;

    int port = index >> 2;
    int slot = index - (port << 2);

    char copy[128] = {0};

    /* __strcpy is unbounded, so check before copying rather than after. The
       length is logged instead of the path: this is the one case where the
       path is known to be over-long, so printing it is exactly what would
       overflow the log buffer. */
    int nameLen = __strlen((char*)name);

    if (nameLen >= (int)sizeof(copy))
    {
        LOG("mStorageMC: path too long, %d chars, max %d\n",
            nameLen, (int)sizeof(copy) - 1);
        return -1;
    }

    __strcpy(copy, name);
    char* dir = _dirname(copy);

    char mcDir[128] = {0};
    _sprintf(mcDir, "%s/*", dir);

    /* Use the game's own wrapper rather than sceMcGetDir + sceMcSync by hand.
       sceMcGetDir can fail WITHOUT queueing anything, and sceMcSync in mode 0
       blocks and returns 1 or -1 but never 0 - so the obvious "queue once, then
       poll until it says 1" shape hangs forever on any queue failure. mcGetDir
       locks, retries the queue, waits, unlocks, and hands back the entry count
       or a negative error. */
    sceMcTblGetDir table[25];
    int entries = mcGetDir(port, slot, mcDir, 0, 25, table);

    LOG("mStorageMC: port=%d, slot=%d, pattern=%s, entries=%d\n",
        port, slot, mcDir, entries);

    if (entries <= 0)
        return -1;

    if (entries > 25)
        entries = 25;

    char* fileName = get_file_name((char*)name);

    for (int i = 0; i < entries; i++)
    {
#if HOSTFS_PRINT
        LOG("  [%d] '%s' %u bytes\n", i, (char*)table[i].EntryName, table[i].FileSizeByte);
#endif
        if (!__strcmp((char*)table[i].EntryName, fileName))
            return (int)table[i].FileSizeByte;
    }

    /* Split, and only one bounded string per line: `name` and `fileName` are
       each up to 127 bytes, and together with a prefix they would overflow
       LOG's 256-byte buffer. */
    LOG("mStorageMC: not found among %d entries\n", entries);
    LOG("  wanted '%s'\n", fileName);

    return -1;
}

unsigned int HOOK__mStorageMC_getFileSize(void* this, char* name)
{
    // Note: game immediately reuses return value into a malloc, so do not return something like -1
    int size = MC_FileSizeBytes(this, name);

    if (size < 0)
        return 0;

    /* One MORE than the file holds, on purpose.

       The caller mallocs exactly what we return, reads exactly that many bytes
       into it, then builds its adhoc string from strlen() of the buffer
       (0x511040, called at 0x3B6C9C). Nothing zeroes the buffer, so without a
       spare byte the string would run off the end of the file. The extra byte
       is where HOOK__mStorageMC_read puts the terminator. */
    return (unsigned int)(size + 1);
}
