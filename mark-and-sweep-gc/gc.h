#pragma once

//#define GC_DEBUG	// for debug

void GC_init();
void* GC_alloc(size_t reqSize);