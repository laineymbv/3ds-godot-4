// Include libctru from Godot sources. libctru's `Thread` typedef clashes with
// Godot's Thread class, so it is renamed while the SDK headers are parsed.
#pragma once
#define Thread CtruThread
#include <3ds.h>
#undef Thread
