// Compile/runtime checks only constants and types from the official installed SDK.
// No controller object, WinRT factory, COM instance or Native API call.
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#include <windows.h>
#include <Xinput.h>
#include <joystickapi.h>
#include <windows.gaming.input.h>
#include <cstddef>
#include <cstdio>
#include <type_traits>
using Caps=DWORD(WINAPI*)(DWORD,DWORD,XINPUT_CAPABILITIES*);
using Joy=MMRESULT(WINAPI*)(UINT,LPJOYINFOEX);
static_assert(std::is_convertible<decltype(&XInputGetCapabilities),Caps>::value,"official capabilities ABI");
static_assert(std::is_same<decltype(&joyGetPosEx),Joy>::value,"official joy ABI");
using GamepadVtable=__x_ABI_CWindows_CGaming_CInput_CIGamepadVtbl;
static_assert(offsetof(GamepadVtable,GetCurrentReading)==8*sizeof(void*),"SDK named member establishes WGI slot, not a guessed projection");
static_assert(sizeof(XINPUT_CAPABILITIES)==20,"official capability packet size");
static_assert(sizeof(JOYINFOEX)==52,"official joystick packet size");
int main(){std::puts("{\"passed\":5,\"failed\":0,\"completed\":true,\"scope\":\"official SDK ABI types only; no API calls or controller object\"}");return 0;}
