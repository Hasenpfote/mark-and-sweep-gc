/**
	Mark and sweep garbage collection sample
	Windows7(32bit)スレッド非対応
 */
#include <stdio.h>
#include <conio.h>
#include <windows.h>
#include <psapi.h>
#include "gc.h"


#define GC_TEST	// コメントアウトでメモリリークを確認

static int WorkSize(DWORD procId = 0);
static int VirtualWalk(DWORD procId = 0);

int main(int argc, char* argv[]){
//	VirtualWalk();

	printf("*** before\n");
	WorkSize();
	GC_init();

	void* p = NULL;
	for(int i = 0;  i < 10000; i++){
#ifdef GC_TEST
		p = GC_alloc(1000);
#else
		p = malloc(1000);	// memory leak
#endif
	}

	printf("*** after\n");
	WorkSize();

	return 1;
}

/**
   指定プロセスのメモリ使用量
 */
static int WorkSize(DWORD procId){
	if(procId == 0)
		procId = GetCurrentProcessId();
	
	HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, procId);
    if(hProc == NULL){
        printf("指定されたプロセスIDのプロセスが見つかりません。\n");
        return -1;
    }

	if(procId == GetCurrentProcessId())
		printf("process id: %d(current)\n", procId);
	else
		printf("process id: %d\n", procId);

    PROCESS_MEMORY_COUNTERS pmc;
	if(GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))){
		printf("ページフォールト数          : %10lu\n", pmc.PageFaultCount);
		printf("最大のワーキングセットサイズ: %10lu bytes\n", pmc.PeakWorkingSetSize);
		printf("現在のワーキングセットサイズ: %10lu bytes\n", pmc.WorkingSetSize);
		printf("最大のページプール使用量    : %10lu bytes\n", pmc.QuotaPeakPagedPoolUsage);
		printf("現在のページプール使用量    : %10lu bytes\n", pmc.QuotaPagedPoolUsage);
		printf("最大の非ページプール使用量  : %10lu bytes\n", pmc.QuotaPeakNonPagedPoolUsage);
		printf("現在の非ページプール使用量  : %10lu bytes\n", pmc.QuotaNonPagedPoolUsage);
		printf("最大のページファイル使用量  : %10lu bytes\n", pmc.PeakPagefileUsage);
		printf("現在のページファイル使用量  : %10lu bytes\n", pmc.PagefileUsage);
	}
    CloseHandle(hProc);

	return 0;
}

/**
   指定プロセスのメモリダンプ
 */
static int VirtualWalk(DWORD procId){
	if(procId == 0)
		procId = GetCurrentProcessId();
	
	HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, procId);
    if(hProc == NULL){
        printf("指定されたプロセスIDのプロセスが見つかりません。\n");
        return -1;
    }

	if(procId == GetCurrentProcessId())
		printf("process id: %d(current)\n", procId);
	else
		printf("process id: %d\n", procId);

	SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    DWORD mask = sysinfo.dwAllocationGranularity - 1;

	unsigned char *p = (unsigned char *)0x0;

	printf("No  Addr       Size(bytes)  Pages  AllocBase  状態      アクセス許可          タイプ/モジュール名\n");
    printf("=================================================================================================\n");

	MEMORY_BASIC_INFORMATION mbi;
	DWORD no = 0;
	while(1){
        if(VirtualQueryEx( hProc, p, &mbi, sizeof(mbi))){
            DWORD granuOffset = ((unsigned long)mbi.BaseAddress & mask);
            bool atBoundary = granuOffset == 0;
            if(mbi.State == MEM_FREE && !atBoundary){
                SIZE_T size = sysinfo.dwAllocationGranularity - granuOffset;
                if(mbi.RegionSize > size)
					mbi.RegionSize = size;
            }
            
			printf("%03d 0x%08lx  %10ld%7d  0x%08lx ", no++, mbi.BaseAddress, mbi.RegionSize, mbi.RegionSize / sysinfo.dwPageSize, mbi.AllocationBase);
            
			switch(mbi.State){
            case MEM_COMMIT: printf("コミット  "); break;
            case MEM_RESERVE:printf("予約      "); break;
            case MEM_FREE:
                if(atBoundary)	printf("フリー    ");
                else            printf("予約余り  ");
                break;
            default:	printf("          ");
            }
            
            switch(mbi.Protect){
            case PAGE_NOACCESS:	printf("アクセス不可          ");
                break;
            case PAGE_READONLY:	printf("読み込みのみ可        ");
                break;
            case PAGE_READWRITE:printf("読み書き可            ");
                break;
            case PAGE_READWRITE | PAGE_GUARD:	printf("読み書き可(ガード)    ");	break;
            case PAGE_WRITECOPY:				printf("書き込み時コピー      ");	break;
            case PAGE_EXECUTE:					printf("実行可                ");	break;
            case PAGE_EXECUTE_READ:				printf("実行/読み込みのみ可   ");	break;
            case PAGE_EXECUTE_READWRITE:		printf("実行/読み書き可       ");	break;
            case PAGE_EXECUTE_WRITECOPY:		printf("実行/書き込み時コピー ");	break;
            default:
                if( mbi.Protect != 0){
                    printf("保護属性=0x%08lx   ", mbi.Protect);
                }
                else{
                printf("                      ");
                }
            }
            switch(mbi.Type){
            case MEM_IMAGE:		printf("イメージ    ");	break;
            case MEM_MAPPED:	printf("マップ      ");	break;
            case MEM_PRIVATE:	printf("プライベート");	break;
            default:			printf("            ");
			}

			if(mbi.State == MEM_COMMIT
			&&(mbi.Protect == PAGE_EXECUTE_READWRITE || mbi.Protect == PAGE_READWRITE)
			&& mbi.Type == MEM_IMAGE){
				printf(" ※");
			}
			
			printf("\n");
            
            p = (unsigned char*)mbi.BaseAddress + mbi.RegionSize;
            if(p == 0)
				break;
        }
        else{
            printf("0x%08lx     --- アクセス不許可\n", p);
            break;
        }
    }
    printf( "---------------------------------------------------------------------------------------------------\n\n");
    CloseHandle(hProc);
    return 0;
}