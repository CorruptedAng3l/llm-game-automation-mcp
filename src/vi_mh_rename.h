// vi_mh_rename.h - force-included into every vendored MinHook TU compiled for vinput.
// Gives vinput a PRIVATE MinHook instance (own heap, own hook table, own symbol names), so it can be
// compiled into a host DLL (your own tool or mod) that already links its own MinHook without duplicate symbols and without
// MH_ERROR_ALREADY_INITIALIZED / ALREADY_CREATED fights. Two instances hooking the same function chain
// correctly (the second one relocates the first one's jmp into its trampoline).
#pragma once
#define MH_Initialize            VIMH_Initialize
#define MH_Uninitialize          VIMH_Uninitialize
#define MH_CreateHook            VIMH_CreateHook
#define MH_CreateHookApi         VIMH_CreateHookApi
#define MH_CreateHookApiEx       VIMH_CreateHookApiEx
#define MH_RemoveHook            VIMH_RemoveHook
#define MH_EnableHook            VIMH_EnableHook
#define MH_DisableHook           VIMH_DisableHook
#define MH_QueueEnableHook       VIMH_QueueEnableHook
#define MH_QueueDisableHook      VIMH_QueueDisableHook
#define MH_ApplyQueued           VIMH_ApplyQueued
#define MH_StatusToString        VIMH_StatusToString
#define InitializeBuffer         VIMH_InitializeBuffer
#define UninitializeBuffer       VIMH_UninitializeBuffer
#define AllocateBuffer           VIMH_AllocateBuffer
#define FreeBuffer               VIMH_FreeBuffer
#define IsExecutableAddress      VIMH_IsExecutableAddress
#define CreateTrampolineFunction VIMH_CreateTrampolineFunction
#define hde64_disasm             VIMH_hde64_disasm
#define hde32_disasm             VIMH_hde32_disasm
#define hde64_table              VIMH_hde64_table
#define hde32_table              VIMH_hde32_table
