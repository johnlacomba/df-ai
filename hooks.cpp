// Lockstep mode is disabled for the Steam/DFHack 53.x port.
// The original hooks.cpp patched SDL_GetTicks and DF's main loop
// for headless simulation. This stub provides no-op implementations
// of the exported functions so the rest of df-ai compiles and links.

#include "hooks.h"

volatile bool lockstep_hooked = false;
volatile bool disabling_plugin = false;
volatile bool unloading_plugin = false;

void Hook_Update()
{
}

void Hook_Shutdown()
{
}

void Hook_Shutdown_Now()
{
}
