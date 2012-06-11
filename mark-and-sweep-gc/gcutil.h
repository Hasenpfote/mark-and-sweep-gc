#pragma once

static inline uintptr_t alignment(uintptr_t rawAddress, size_t align){
    align--;
    return (rawAddress + align) & ~align;
}

static inline void* alignment(void* rawAddress, size_t align){
	align--;
	return reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(rawAddress) + align) & ~align);
}

void* getStackBase(size_t pageSize);
void* getStackMin(void* s);