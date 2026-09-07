#pragma once

#include "core/Target.h"

#include "../GameStructs/Adhoc.h"

// Ref counter
extern void (*RefCounter_ref)(RefCounter* this_);
extern void (*RefCounter_unref)(RefCounter* this_);

// HValue
extern void (*HValue_dtor)(void* this_, int flag);;

// HInt
extern void (*HInt_HInt)(HInt* this_, int value);
extern void (*HInt_dtor)(HInt* this_, int flag);

// HFloat
extern void (*HFloat_HFloat)(HFloat* this_, float value);
extern void (*HFloat_dtor)(HFloat* this_, int flag);

// HString. Takes a POINTER to a std::string, not a char* - see STD_STRING.
extern void (*HString_HString)(HString* this_, void* stdString);

/*
    HNil. Same shape as HInt_HInt with nothing to carry: it takes the ADDRESS of
    the caller's handle variable and fills it. Release it with HValue_dtor.

    Hand this to script rather than leaving a return value untouched. A native
    that never writes its return slot yields a null handle, and while adhoc's own
    `nil` is also a null, a null is only safe in a comparison - `if (x)` on one
    faults. See ADDR_HNil_HNil in the target header for the disassembly.
*/
extern void (*HNil_HNil)(HObject* this_);

/*
    Byte offsets of the conversion entries in an adhoc value's vtable.

    Every value type implements all of them at the same offsets, so a caller can
    convert a value without knowing its type. Each entry is 8 bytes,
    { int16 adjustor, int16 pad, void* fn }, and the game invokes one like this
    (hObject::toInt, 0x4F2E88):

        object = *slot;  vtable = *(object + 4);  entry = vtable + offset;
        fn(object + *(short*)entry)

    CSTR is the exception to "every type": hInt and hFloat do NOT override it
    and inherit a default returning the empty string, so it is only good for a
    value already known to be a string. TOSTRING is the generic one.
*/
#define ADHOC_VT_TOSTRING 0x18
#define ADHOC_VT_CSTR     0x60
#define ADHOC_VT_TOINT    0x68
#define ADHOC_VT_TOFLOAT  0x70

// ADHOC
extern void (*ADHOC_Deallocate)(void* pool, void *buffer, int size);

// HClassInit
typedef void (*HClassInit_Create_cb)();
typedef void (*HClassInit_Init_cb)();
typedef void (*HClassInit_Cleanup_cb)();
extern void (*HClassInit_HClassInit)(const HClassInit* this_, const char* name, int size, HClassInit_Create_cb create, HClassInit_Init_cb init, HClassInit_Cleanup_cb cleanup);
extern struct hClass* (*HClassInit_CreateClass)();

// hClass
extern void (*hClass_setSuperClassID)(hClass* this_, hClass* super);

// hObject
extern struct hClass* (*hObject_GetClassID)();

// hModule
/* Adhoc function callback handler */
typedef void (*Adhoc_function_cb)(HObject* return_value, int argc, hObject** argv);

/* Defines a new adhoc function in this module. */
extern void (*hModule_defineFunction)(void*, hModule* thisModule, char* functionName, Adhoc_function_cb function);

/* Adhoc method callback handler */
typedef void (*Adhoc_method_cb)(HObject* return_value, HObject* this_, int argc, hObject** argv);

/* Defines a new adhoc method in this module. */
extern void (*hModule_defineMethod)(void*, hModule* thisModule, char* methodName, Adhoc_method_cb method);

/* Defines a new adhoc static member (i.e constants). */
extern void (*hModule_defineStatic)(void*, hModule* thisModule, char* staticName, HObject* value);

/* Adhoc attrubute callback handler */
typedef void (*Adhoc_attribute_cb)(HObject* return_value, HObject* this_, HObject* unk, int argc, hObject** argv);
extern void (*hModule_defineAttribute)(void*, hModule* thisModule, char* attributeName, Adhoc_attribute_cb getter, Adhoc_attribute_cb setter);

extern void (*hModule_defineClass)(void*, hModule* previous, hModule* classId);
