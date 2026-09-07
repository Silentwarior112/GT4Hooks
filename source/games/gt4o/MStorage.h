#pragma once

#include "GameStructs/Adhoc.h"
#include "GameFunctions/Adhoc.h"
#include "Adhoc/Decl.h"

/*
    Make a shorter write actually replace the file rather than overwrite its
    front. Set to 0 to leave writes exactly as the game does them.

    Worth knowing before turning this off or on: mOption::save reaches the same
    vtable slot (0x163DFC), so this hook sits between the player's settings and
    their memory card. It only ever pads - it never shortens or deletes - and
    mOption writes a constant length, so its saves are untouched in practice.
*/
#ifndef MSTORAGE_WRITE_TRUNCATES
#define MSTORAGE_WRITE_TRUNCATES 1
#endif

/* Register the plugin's own members on the MStorage adhoc module. */
#ifndef MSTORAGE_EXTRA_MEMBERS
#define MSTORAGE_EXTRA_MEMBERS 1
#endif

void MStorage_InstallHooks();

/*
    Registers mStorage's own "mkdir" and then the plugin's extra members. Its
    signature matches a METHOD registration, which is what the hooked call is.
*/
void HOOK_ExtendMStorage(void* tempHValue, hModule* module, char* name, Adhoc_method_cb method);

/* Proves the registration works end to end without touching the card. */
void m_ping(HObject* return_value, HObject* this_, int argc, hObject** argv);

/*
    Takes one value of any type and returns it, having gone through the same
    text conversion the stored format will use. Proves type detection, the
    vtable conversion slots, number formatting and parsing, and handle
    construction - all without touching the memory card.
*/
void m_echoVar(HObject* return_value, HObject* this_, int argc, hObject** argv);

/*
    Named values of the three adhoc types, kept as lines of text in one file on
    the memory card. See the block comment above KV_SEP in MStorage.c for the
    file format and what it refuses to store.

      storage.setVar(path, name, value)          -> 1 on success, 0 otherwise
      storage.getVar(path, name [, fallback])    -> the value, fallback, or nil
      storage.delVar(path, name)                 -> 1 if removed, 0 otherwise
*/
void m_setVar(HObject* return_value, HObject* this_, int argc, hObject** argv);
void m_getVar(HObject* return_value, HObject* this_, int argc, hObject** argv);
void m_delVar(HObject* return_value, HObject* this_, int argc, hObject** argv);

/*
    The same store, a whole table at a time. `table` is an adhoc array of
    [name, value] pairs, and these cost ONE card read and ONE card write for the
    lot rather than one of each per variable.

      storage.setVars(path, table)   -> 1 if the whole table was written, else 0
      storage.getVars(path, table)   -> how many rows the file filled in, or -1

    setVars is all or nothing: one unusable row and nothing is written.
    getVars fills each row's value in place and leaves a row it has no entry for
    holding whatever the script already put there, so the table's own contents
    are the per-row fallbacks.

    getVars returns -1 when the card could not be read or the file is not a kv
    file - which a script MUST tell apart from 0, "read fine, nothing stored".
    Saving after a -1 would write defaults over a good save. -1 is truthy in
    adhoc, so the test is `n < 0`.
*/
void m_setVars(HObject* return_value, HObject* this_, int argc, hObject** argv);
void m_getVars(HObject* return_value, HObject* this_, int argc, hObject** argv);

/*
    The same store, held in the plugin's own memory so it outlives an adhoc
    context. Values loaded into a script variable die when the game leaves the
    menu; these do not.

      storage.loadVars(path)              card -> memory. Variables loaded, or -1
      storage.saveVars(path)              memory -> card. 1 or 0

      MStorage::getExtraData(name [, fallback])   the value, fallback, or nil
      MStorage::setExtraData(name, value)         1 or 0

    The last two are FUNCTIONS, called with :: and needing no storage object,
    because they touch no card and a race-context script may not be able to
    build one. saveVars only writes to the path loadVars read - that check is
    the whole of its safety, since it deliberately does not read before writing.

    Do not mix these with the file-backed members on the same file: any of those
    reloads the shared buffer and discards unsaved edits (loudly - it logs).
*/
void m_loadVars(HObject* return_value, HObject* this_, int argc, hObject** argv);
void m_saveVars(HObject* return_value, HObject* this_, int argc, hObject** argv);
void f_getExtraData(HObject* return_value, int argc, hObject** argv);
void f_setExtraData(HObject* return_value, int argc, hObject** argv);

int HOOK__mStorageMC_read(void* this, const char* name, char* buf, int len);
int HOOK__mStorageMC_write(void* this, const char* name, const char* buf, int len);
unsigned int HOOK__mStorageMC_getFileSize(void* this_, char* name);
