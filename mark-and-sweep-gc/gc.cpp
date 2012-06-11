
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <setjmp.h>
#include <windows.h>
#include "gcutil.h"
#include "gc.h"

typedef struct tagHeader{
	// �A���C�����g���l�����Ă���̂Ō^��ύX���Ȃ�����
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
 * �|�C���^��GC���Ǘ�������̂��𒲂ׂ�
 * @param ptr �����ΏۂƂ���|�C���^
 * @return ��������GC�q�[�v�ւ̃|�C���^
 */
static Heap* isPointerToHeap(void* ptr){
	// ToDO: �L���b�V�����������Ă݂�̂�����
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
 * �A�����鎟�̃w�b�_���擾
 * @param p ���݂̃w�b�_
 * @return ���̃w�b�_
 */
static Header* getNextHeader(Header* p){
	return reinterpret_cast<Header*>(reinterpret_cast<uintptr_t>(p + 1) + p->size);
}

/**
 * GC���Ǘ�����q�[�v������w�b�_���擾
 * @param heap ptr����������GC���Ǘ�����q�[�v
 * @param ptr �����ΏۂƂ���|�C���^
 * @return ptr����������u���b�N�̃w�b�_�ւ̃|�C���^
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
 * GC�p�̃q�[�v���m��
 * @param reqSize �v���T�C�Y
 * @return �m�ۂ���Heap�̐擪�w�b�_�ւ̃|�C���^
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
 * GC�p�̃q�[�v���m��
 * @param reqSize �v���T�C�Y
 * @return �A���C�����g���ꂽ�q�[�v�ւ̃|�C���^
 */
void* GC_alloc(size_t reqSize){
	if(reqSize == 0)
		return NULL;

	reqSize = alignment(reqSize, sizeof(void*));

	if(freelist == NULL){
		// �v���T�C�Y��菭���傫�߂Ɋm�ۂ���
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
			// �҂�����̃T�C�Y������ꍇ
			prev->next = current->next;
			current->flags = FLAG_ALLOC;
			current->next = NULL;
			if(current == freelist)
				freelist = NULL;
			else
				freelist = prev;	// ���[�v�������璷�ɂȂ邽��

			return reinterpret_cast<void*>(current + 1);
		}
		else
		if(current->size > (sizeof(Header) + reqSize)){	// >= ���� Header �̎��� Header �����ăT�C�Y0�ɂȂ�̂����
			// �󂫃T�C�Y�ɂ�Ƃ肪����ꍇ
			current->size -= sizeof(Header) + reqSize;
			current = getNextHeader(current);
			current->size = reqSize;
			current->flags = FLAG_ALLOC;
			current->next = NULL;
			freelist = prev;		// ���[�v�������璷�ɂȂ邽��
			return reinterpret_cast<void*>(current + 1);
		}
		else
		if(current == freelist){
			// �󂫃T�C�Y���Ȃ��ꍇ
			if(!doGc){
				// GC ���s���󂫗e�ʂ��҂�
				// GC �ŋ󂫗e�ʂ��m�ۂł����ꍇ�Afreelist->next �ɕω�������
				// �ω����Ȃ��ꍇ�͈�����ăq�[�v�̊g�������݂���
				GC_collect();
				doGc = true;
			}
			else{
				// �q�[�v�̊g�������݂�
				// GC ���s���Ă��󂫂��o���Ȃ��ꍇ�̓q�[�v�̊g�������݂�
				// �q�[�v���g���ł���� freelist->next �ɕω�������
				// �g���ł��Ȃ���ΏI������
				// �v���T�C�Y��菭���傫�߂Ɋm�ۂ���
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
 * GC�p�̃q�[�v�����
 * @param ptr GC_Alloc�Ŋm�ۂ��ꂽ�q�[�v�ւ̃|�C���^
 */
static void GC_free(void* ptr){

	Header* target = reinterpret_cast<Header*>(ptr) - 1;

	if(freelist != NULL){
		Header* header = freelist;
		Header* next;
		// �󂫃��X�g���ɑ��݂��邩
		do{
			next = header->next;
			if((target >= header) && (target < header->next)){
				break;
			}
			if(next != freelist){
				header = next;
			}
		}while(next != freelist);

		// target ���猩�� header->next ����������ŘA���z�u����Ă��邩
		if(getNextHeader(target) == header->next){
			// target �ɓ�������
			target->size += header->next->size + sizeof(Header);
			target->next = header->next->next;
		}
		else{
			target->next = header->next;
		}

		// header ���猩�� target ����������ŘA���z�u����Ă��邩
		if(getNextHeader(header) == target){
			// header �ɓ�������
			header->size += target->size + sizeof(Header);
			header->next = target->next;
		}
		else{
			header->next = target;
		}
		// GCAlloc ���ŏ璷�ȃ��[�v�T�����s��Ȃ��悤
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
 * �Q�Ƃ���Ă���q�[�v���}�[�N����
 * @param ptr �����ΏۂƂ���|�C���^
 */
static void GC_mark(void* ptr){
	Heap* heap = isPointerToHeap(ptr);
	if(!heap)
		return;	// GC���Ǘ�����q�[�v�ł͂Ȃ�
	Header* header = getHeader(heap, ptr);
	if(!header)
		return;	// �w�b�_���擾�ł��Ȃ�
	if(!(header->flags & FLAG_ALLOC))

		return;	// GC�ɂ���Ċm�ۂ���Ă��Ȃ�
	if(header->flags & FLAG_MARK)
		return;	// ���Ƀ}�[�L���O����Ă���

	header->flags |= FLAG_MARK;
#ifdef GC_DEBUG
	printf("GC_Mark: marked ptr %p, header %p, heap %p\n", ptr, header, heap);
#endif
	// �ċA�ɂȂ邪
	// �q�[�v����q�[�v�ւ̎Q�Ƃ�H��
	GC_markRange(reinterpret_cast<void*>(header + 1), reinterpret_cast<void*>(getNextHeader(header)));
}

/**
 * �C�ӂ͈̔͂��}�[�N����
 * @param start �J�n�A�h���X�ւ̃|�C���^
 * @param end �I���A�h���X�ւ̃|�C���^
 */
static void GC_markRange(void* start, void* end){
	const uintptr_t e = reinterpret_cast<uintptr_t>(end);
	for(uintptr_t p = reinterpret_cast<uintptr_t>(start); p < e; p += sizeof(void*)){
		GC_mark(*reinterpret_cast<void**>(p));
	}
}

/**
 * ���W�X�^��ΏۂɃ}�[�N����
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
 * �X�^�b�N��ΏۂɃ}�[�N����
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
 * �X�C�[�v���s��
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
				if(p->flags & FLAG_MARK){	// �}�[�N����Ă���Εی�
					p->flags &= ~FLAG_MARK;
				}
				else{					// �}�[�N����Ă��Ȃ���Ή��
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
 * �K�[�x�[�W�R���N�g
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
 * ���[�g�̓o�^
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
 * �f�[�^�Z�O�����g�̓o�^
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
 * GC�̏�����
 */
void GC_init(){
	GetSystemInfo(&sysinfo);
	page_size = sysinfo.dwPageSize;
	heaps_used = 0;
	stack_base = getStackBase(page_size);
	roots_used = 0;
	GC_register_dynamic_libraries();
}