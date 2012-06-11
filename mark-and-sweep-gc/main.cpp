/**
	Mark and sweep garbage collection sample
	Windows7(32bit)�X���b�h��Ή�
 */
#include <stdio.h>
#include <conio.h>
#include <windows.h>
#include <psapi.h>
#include "gc.h"


#define GC_TEST	// �R�����g�A�E�g�Ń��������[�N���m�F

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
   �w��v���Z�X�̃������g�p��
 */
static int WorkSize(DWORD procId){
	if(procId == 0)
		procId = GetCurrentProcessId();
	
	HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, procId);
    if(hProc == NULL){
        printf("�w�肳�ꂽ�v���Z�XID�̃v���Z�X��������܂���B\n");
        return -1;
    }

	if(procId == GetCurrentProcessId())
		printf("process id: %d(current)\n", procId);
	else
		printf("process id: %d\n", procId);

    PROCESS_MEMORY_COUNTERS pmc;
	if(GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))){
		printf("�y�[�W�t�H�[���g��          : %10lu\n", pmc.PageFaultCount);
		printf("�ő�̃��[�L���O�Z�b�g�T�C�Y: %10lu bytes\n", pmc.PeakWorkingSetSize);
		printf("���݂̃��[�L���O�Z�b�g�T�C�Y: %10lu bytes\n", pmc.WorkingSetSize);
		printf("�ő�̃y�[�W�v�[���g�p��    : %10lu bytes\n", pmc.QuotaPeakPagedPoolUsage);
		printf("���݂̃y�[�W�v�[���g�p��    : %10lu bytes\n", pmc.QuotaPagedPoolUsage);
		printf("�ő�̔�y�[�W�v�[���g�p��  : %10lu bytes\n", pmc.QuotaPeakNonPagedPoolUsage);
		printf("���݂̔�y�[�W�v�[���g�p��  : %10lu bytes\n", pmc.QuotaNonPagedPoolUsage);
		printf("�ő�̃y�[�W�t�@�C���g�p��  : %10lu bytes\n", pmc.PeakPagefileUsage);
		printf("���݂̃y�[�W�t�@�C���g�p��  : %10lu bytes\n", pmc.PagefileUsage);
	}
    CloseHandle(hProc);

	return 0;
}

/**
   �w��v���Z�X�̃������_���v
 */
static int VirtualWalk(DWORD procId){
	if(procId == 0)
		procId = GetCurrentProcessId();
	
	HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, procId);
    if(hProc == NULL){
        printf("�w�肳�ꂽ�v���Z�XID�̃v���Z�X��������܂���B\n");
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

	printf("No  Addr       Size(bytes)  Pages  AllocBase  ���      �A�N�Z�X����          �^�C�v/���W���[����\n");
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
            case MEM_COMMIT: printf("�R�~�b�g  "); break;
            case MEM_RESERVE:printf("�\��      "); break;
            case MEM_FREE:
                if(atBoundary)	printf("�t���[    ");
                else            printf("�\��]��  ");
                break;
            default:	printf("          ");
            }
            
            switch(mbi.Protect){
            case PAGE_NOACCESS:	printf("�A�N�Z�X�s��          ");
                break;
            case PAGE_READONLY:	printf("�ǂݍ��݂̂݉�        ");
                break;
            case PAGE_READWRITE:printf("�ǂݏ�����            ");
                break;
            case PAGE_READWRITE | PAGE_GUARD:	printf("�ǂݏ�����(�K�[�h)    ");	break;
            case PAGE_WRITECOPY:				printf("�������ݎ��R�s�[      ");	break;
            case PAGE_EXECUTE:					printf("���s��                ");	break;
            case PAGE_EXECUTE_READ:				printf("���s/�ǂݍ��݂̂݉�   ");	break;
            case PAGE_EXECUTE_READWRITE:		printf("���s/�ǂݏ�����       ");	break;
            case PAGE_EXECUTE_WRITECOPY:		printf("���s/�������ݎ��R�s�[ ");	break;
            default:
                if( mbi.Protect != 0){
                    printf("�ی쑮��=0x%08lx   ", mbi.Protect);
                }
                else{
                printf("                      ");
                }
            }
            switch(mbi.Type){
            case MEM_IMAGE:		printf("�C���[�W    ");	break;
            case MEM_MAPPED:	printf("�}�b�v      ");	break;
            case MEM_PRIVATE:	printf("�v���C�x�[�g");	break;
            default:			printf("            ");
			}

			if(mbi.State == MEM_COMMIT
			&&(mbi.Protect == PAGE_EXECUTE_READWRITE || mbi.Protect == PAGE_READWRITE)
			&& mbi.Type == MEM_IMAGE){
				printf(" ��");
			}
			
			printf("\n");
            
            p = (unsigned char*)mbi.BaseAddress + mbi.RegionSize;
            if(p == 0)
				break;
        }
        else{
            printf("0x%08lx     --- �A�N�Z�X�s����\n", p);
            break;
        }
    }
    printf( "---------------------------------------------------------------------------------------------------\n\n");
    CloseHandle(hProc);
    return 0;
}