
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <setjmp.h>
#include <windows.h>
#include "gcutil.h"
#include "gc.h"

typedef struct tagHeader{
	// アライメントを考慮しているので型を変更しないこと
	size_t flags;
	size_t size;
	tagHeader* next;
}Header;

typedef struct tagHeap{
	Header* slot;
	size_t size;
}Heap;

typedef struct tagRoot{
	void* start;
	void* end;
}Root;

#define APPEND_SIZE (sizeof(Header) + sizeof(void*))
#define FLAG_ALLOC	0x1
#define FLAG_MARK	0x2
#define MAX_HEAPS 100
static Heap heaps[MAX_HEAPS];
static int heaps_used = 0;
static Header* freelist;
static _SYSTEM_INFO sysinfo;
static size_t page_size;
static void* stack_base;
#define MAX_ROOTS 100
static Root roots[MAX_ROOTS];
static int roots_used = 0;

/**
 * ポインタがGCが管理するものかを調べる
 * @param ptr 調査対象とするポインタ
 * @return 所属するGCヒープへのポインタ
 */
static Heap* isPointerToHeap(void* ptr){
	// ToDO: キャッシュを実装してみるのもいい
	const int used = heaps_used;
	for(int i = 0; i < used; i++){
		const uintptr_t base = reinterpret_cast<uintptr_t>(heaps[i].slot + 1);
		if((reinterpret_cast<uintptr_t>(ptr) >= base) &&
		   (reinterpret_cast<uintptr_t>(ptr)) < (base + heaps[i].size)){
			return &heaps[i];
		}
	}
	return NULL;
}

/**
 * 連続する次のヘッダを取得
 * @param p 現在のヘッダ
 * @return 次のヘッダ
 */
static Header* getNextHeader(Header* p){
	return reinterpret_cast<Header*>(reinterpret_cast<uintptr_t>(p + 1) + p->size);
}

/**
 * GCが管理するヒープ内からヘッダを取得
 * @param heap ptrが所属するGCが管理するヒープ
 * @param ptr 調査対象とするポインタ
 * @return ptrが所属するブロックのヘッダへのポインタ
 */
static Header* getHeader(const Heap* heap, void* ptr){
	const Header* end = reinterpret_cast<Header*>(reinterpret_cast<uintptr_t>(heap->slot + 1) + heap->size);
	Header* next;
	for(Header* p = heap->slot; p < end; p = next){
		next = getNextHeader(p);
		if((ptr >= reinterpret_cast<void*>(p + 1)) &&
		   (ptr < reinterpret_cast<void*>(next))){
			   return p;
		}
	}
	return NULL;
}

/**
 * GC用のヒープを確保
 * @param reqSize 要求サイズ
 * @return 確保したHeapの先頭ヘッダへのポインタ
 */
static Header* addHeap(size_t reqSize){
	if(heaps_used >= MAX_HEAPS){
		fputs("addHeap: overflowed\n", stderr);
		abort();
	}

	size_t actualSize = APPEND_SIZE + reqSize;
	actualSize = alignment(actualSize, page_size);
	void* p = calloc(actualSize, sizeof(char));

	if(!p){
		fputs("addHeap: out of memory\n", stderr);
		return NULL;
	}
#ifdef GC_DEBUG
	printf("addHeap: expansion %ld bytes\n", actualSize);
#endif
	Header* header = reinterpret_cast<Header*>(alignment(p, sizeof(void*)));
	header->size = actualSize - APPEND_SIZE;
	header->next = header;

	Heap* heap = &heaps[heaps_used];
	heap->slot = header;
	heap->size = header->size;
	heaps_used++;

	return header;
}

static void GC_collect();
static void GC_free(void* ptr);
/**
 * GC用のヒープを確保
 * @param reqSize 要求サイズ
 * @return アライメントされたヒープへのポインタ
 */
void* GC_alloc(size_t reqSize){
	if(reqSize == 0)
		return NULL;

	reqSize = alignment(reqSize, sizeof(void*));

	if(freelist == NULL){
		// 要求サイズより少し大きめに確保する
		Header* p = addHeap(reqSize + sizeof(Header) + sizeof(void*));
		if(!p)
			return NULL;
		freelist = p;
	}

	bool doGc = false;
	Header* prev = freelist;
	Header* current = prev->next;
	while(1){
		if(current->size == reqSize){
			// ぴったりのサイズがある場合
			prev->next = current->next;
			current->flags = FLAG_ALLOC;
			current->next = NULL;
			if(current == freelist)
				freelist = NULL;
			else
				freelist = prev;	// ループ検索が冗長になるため

			return reinterpret_cast<void*>(current + 1);
		}
		else
		if(current->size > (sizeof(Header) + reqSize)){	// >= だと Header の次に Header が着てサイズ0になるのを回避
			// 空きサイズにゆとりがある場合
			current->size -= sizeof(Header) + reqSize;
			current = getNextHeader(current);
			current->size = reqSize;
			current->flags = FLAG_ALLOC;
			current->next = NULL;
			freelist = prev;		// ループ検索が冗長になるため
			return reinterpret_cast<void*>(current + 1);
		}
		else
		if(current == freelist){
			// 空きサイズがない場合
			if(!doGc){
				// GC を行い空き容量を稼ぐ
				// GC で空き容量が確保できた場合、freelist->next に変化がある
				// 変化がない場合は一周してヒープの拡張が試みられる
				GC_collect();
				doGc = true;
			}
			else{
				// ヒープの拡張を試みる
				// GC を行っても空きが出来ない場合はヒープの拡張を試みる
				// ヒープが拡張できれば freelist->next に変化がある
				// 拡張できなければ終了する
				// 要求サイズより少し大きめに確保する
				Header* expansion = addHeap(reqSize + sizeof(Header) + sizeof(void*));
				if(expansion){
					GC_free(reinterpret_cast<void*>(expansion + 1));
				}
				else{
					return NULL;
				}
			}
		}

		prev = current;
		current = current->next;
	}
}

/**
 * GC用のヒープを解放
 * @param ptr GC_Allocで確保されたヒープへのポインタ
 */
static void GC_free(void* ptr){

	Header* target = reinterpret_cast<Header*>(ptr) - 1;

	if(freelist != NULL){
		Header* header = freelist;
		Header* next;
		// 空きリスト内に存在するか
		do{
			next = header->next;
			if((target >= header) && (target < header->next)){
				break;
			}
			if(next != freelist){
				header = next;
			}
		}while(next != freelist);

		// target から見て header->next がメモリ上で連続配置されているか
		if(getNextHeader(target) == header->next){
			// target に統合する
			target->size += header->next->size + sizeof(Header);
			target->next = header->next->next;
		}
		else{
			target->next = header->next;
		}

		// header から見て target がメモリ上で連続配置されているか
		if(getNextHeader(header) == target){
			// header に統合する
			header->size += target->size + sizeof(Header);
			header->next = target->next;
		}
		else{
			header->next = target;
		}
		// GCAlloc 内で冗長なループ探索を行わないよう
		freelist = header;
	}
	else{
		freelist = target;
		freelist->next = freelist;
	}

	target->flags = 0x0;
}

static void GC_markRange(void* start, void* end);
/**
 * 参照されているヒープをマークする
 * @param ptr 調査対象とするポインタ
 */
static void GC_mark(void* ptr){
	Heap* heap = isPointerToHeap(ptr);
	if(!heap)
		return;	// GCが管理するヒープではない
	Header* header = getHeader(heap, ptr);
	if(!header)
		return;	// ヘッダが取得できない
	if(!(header->flags & FLAG_ALLOC))

		return;	// GCによって確保されていない
	if(header->flags & FLAG_MARK)
		return;	// 既にマーキングされている

	header->flags |= FLAG_MARK;
#ifdef GC_DEBUG
	printf("GC_Mark: marked ptr %p, header %p, heap %p\n", ptr, header, heap);
#endif
	// 再帰になるが
	// ヒープからヒープへの参照を辿る
	GC_markRange(reinterpret_cast<void*>(header + 1), reinterpret_cast<void*>(getNextHeader(header)));
}

/**
 * 任意の範囲をマークする
 * @param start 開始アドレスへのポインタ
 * @param end 終了アドレスへのポインタ
 */
static void GC_markRange(void* start, void* end){
	const uintptr_t e = reinterpret_cast<uintptr_t>(end);
	for(uintptr_t p = reinterpret_cast<uintptr_t>(start); p < e; p += sizeof(void*)){
		GC_mark(*reinterpret_cast<void**>(p));
	}
}

/**
 * レジスタを対象にマークする
 */
static void GC_markRegister(){
	jmp_buf buf;
	setjmp(buf);
	const size_t size = sizeof(buf) / sizeof(buf[0]);
	for(int i = 0; i < size; i++){
#ifdef GC_DEBUG
		printf("GC_markRegister: %p\n", buf[i]);
#endif
		GC_mark(reinterpret_cast<void*>(buf[i]));
	}
}

/**
 * スタックを対象にマークする
 */
static void GC_markStack(){
	int dummy;
	void* sp = reinterpret_cast<void*>(&dummy);
	void* stack_min = getStackMin(stack_base);
	void* start = ((sp >= stack_min) && (sp < stack_base))? sp : stack_min;
#ifdef GC_DEBUG
	printf("GC_markStack: start %p, end %p\n", start, stack_base);
#endif
	GC_markRange(start, stack_base);
}

/**
 * スイープを行う
 */
static void GC_sweep(){
#ifdef GC_DEBUG
	printf("GC_sweep: start\n");
#endif
	const int used = heaps_used;
	for(int i = 0; i < used; i++){
		const Header* end = reinterpret_cast<Header*>(reinterpret_cast<uintptr_t>(heaps[i].slot + 1) + heaps[i].size);
		for(Header* p = heaps[i].slot; p < end; p = getNextHeader(p)){
			if(p->flags & FLAG_ALLOC){
				if(p->flags & FLAG_MARK){	// マークされていれば保護
					p->flags &= ~FLAG_MARK;
				}
				else{					// マークされていなければ解放
#ifdef GC_DEBUG
					printf("GC_sweep: free %p, %d bytes\n", (p+1), p->size);
#endif
					GC_free(reinterpret_cast<void*>(p + 1));
				}
			}
		}
	}
#ifdef GC_DEBUG
	printf("GC_sweep: end\n");
#endif
}

/**
 * ガーベージコレクト
 */
static void GC_collect(){
#ifdef GC_DEBUG
	printf("GC_collect: start\n");
#endif
	GC_markRegister();
	GC_markStack();

	const int used = roots_used;
	for(int i = 0; i < used; i++){
		GC_markRange(roots[i].start, roots[i].end);
	}

	GC_sweep();
#ifdef GC_DEBUG
	printf("GC_collect: heaps used %d slot\n", heaps_used);
	for(int i = 0; i < heaps_used; i++){
		printf("GC_collect: heap %p, %d bytes\n", heaps[i].slot, heaps[i].size);
	}
	printf("GC_collect: freelist\n");
	if(freelist){
		Header* header = freelist;
		Header* next;
		do{
			next = header->next;
			printf("GC_collect: free %d bytes\n", header->size);
			header = next;
		}while(next != freelist);	
	}
	printf("GC_collect: end\n");
#endif
}

/**
 * ルートの登録
 */
void addRoot(void* start, void* end){
	if(roots_used >= MAX_ROOTS){
		fputs("addRoot: overflowed\n", stderr);
		abort();
	}
	roots[roots_used].start = start;
	roots[roots_used].end = end;
	roots_used++;
#ifdef GC_DEBUG
	printf("addRoot: start %p, end %p used %d\n", start, end, roots_used);
#endif
}

/**
 * データセグメントの登録
 */
static void GC_register_dynamic_libraries(){
	DWORD mask = sysinfo.dwAllocationGranularity - 1;
	unsigned char *p = (unsigned char *)0x0;
	MEMORY_BASIC_INFORMATION mbi;

	while(VirtualQuery(p, &mbi, sizeof(mbi))){
        DWORD granuOffset = ((unsigned long)mbi.BaseAddress & mask);
        bool atBoundary = granuOffset == 0;
        if(mbi.State == MEM_FREE && !atBoundary){
            SIZE_T size = sysinfo.dwAllocationGranularity - granuOffset;
            if(mbi.RegionSize > size)
				mbi.RegionSize = size;
        }
		p = (unsigned char*)mbi.BaseAddress + mbi.RegionSize;

		if(mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_EXECUTE_READWRITE || mbi.Protect == PAGE_READWRITE) && mbi.Type == MEM_IMAGE){
			addRoot(mbi.BaseAddress, reinterpret_cast<void*>(p));
		}
	}
}

/**
 * GCの初期化
 */
void GC_init(){
	GetSystemInfo(&sysinfo);
	page_size = sysinfo.dwPageSize;
	heaps_used = 0;
	stack_base = getStackBase(page_size);
	roots_used = 0;
	GC_register_dynamic_libraries();
}