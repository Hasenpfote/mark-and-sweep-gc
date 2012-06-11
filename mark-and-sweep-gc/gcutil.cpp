#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "gcutil.h"

#define START_ADDRESS(address, align) ((uintptr_t)address & ~(align - 1))
#define OFFSET_ADDRESS(address, align) ((uintptr_t)address & (align - 1))

size_t getWritableLength(void* p, void** base){
	MEMORY_BASIC_INFORMATION mbi;
	size_t result = VirtualQuery(p, &mbi, sizeof(mbi));
	if(result != sizeof(mbi)){
		fputs("getWritableLength:\n", stderr);
		return 0;
	}
	if(base){
		*base = mbi.AllocationBase;
	}
	DWORD protect = (mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE));
	// 書き込めるか
	if((protect == PAGE_READWRITE) || (protect == PAGE_WRITECOPY) || (protect == PAGE_EXECUTE_READWRITE) || (protect == PAGE_EXECUTE_WRITECOPY)){
		// コミットされているか
	   if(mbi.State != MEM_COMMIT)
			return 0;
	   return mbi.RegionSize;
	}
	return 0;
}

void* getStackBase(size_t pageSize){
	int dummy;
	void* sp = reinterpret_cast<void*>(&dummy);
	void* trunc_sp = reinterpret_cast<void*>(START_ADDRESS(sp, pageSize));
	size_t size = getWritableLength(trunc_sp, 0);
	return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(trunc_sp) + size);
}

void* getStackMin(void* s){
	MEMORY_BASIC_INFORMATION mbi;
	VirtualQuery(s, &mbi, sizeof(mbi));
	void* bottom;
	do{
		bottom = mbi.BaseAddress;
		VirtualQuery(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(bottom) - 1), &mbi, sizeof(mbi));
	}while((mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_GUARD));
	return bottom;
}
