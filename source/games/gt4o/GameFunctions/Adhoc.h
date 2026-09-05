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
