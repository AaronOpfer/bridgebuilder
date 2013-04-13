#include <Windows.h>
#include <DbgHelp.h>
#include <stdio.h>

#include "bridgebuilder.h"

#define TEST_MINIMUM_BYTES_DECODED 15

bool run_opcode_test (const char* testName, void* codePtr, int desiredResult) {
	int result = x86_instruction_length(codePtr,false);

	printf("%17s  %d %s= %d\n", 
	       testName, result, (result==desiredResult?"=":"!"), desiredResult);

	return result == desiredResult;
}

bool run_import_test (const char* importName, void* fxnPtr) {
	unsigned char* codePtr = (unsigned char*)fxnPtr;

	int j;

	while ((unsigned long)codePtr < (unsigned long)fxnPtr + TEST_MINIMUM_BYTES_DECODED) {
		j = x86_instruction_length(codePtr,false);
		if (j == -1) {
			printf("Failure!");
			return false;
		}
		//printf("%08X: %d\n", codePtr, j);

		codePtr = &codePtr[j];
	}
	printf("%32s: %d\n", importName, codePtr - (unsigned char*)fxnPtr);
	return true;
}

int main (int argc, char* argv[]) {
	struct {
		const char*  testName; void* codePtr; int desiredResult;
	} testData[] = {
		{ "NOP",             "\x90",                   1 },
		{ "Prefix abuse",    "\xF0\x64\x67\xF0\x90",   5 },
		{ "MOV EAX,[SMALL]", "\x67\xA1\0\0",           4 },
		{ "PUSH reg",        "\x50",                   1 },
		{ "ADD 16",          "\x66\x00\xC0",           3 },
		{ "ADD 32",          "\x01\xC0",               2 },
		{ "ADD 8",           "\x00\xC0",               2 },
		{ "ADD 32[]",        "\x03\x05\0\0\0\0",       6 },
		{ "ADD 16[]",        "\x66\x03\x05\0\0\0\0",   7 },
		{ "ADD R,[R+R]",     "\x03\x0C\x03",           3 },
		{ "MOV R,[R*4+32]",  "\x8B\x04\x85\x02\0\0\0", 7 },
		{ "MOV R,[R*2+R+32]","\x8B\x84\x40\x02\0\0\0", 7 },
		{ "MOV R,[R*4+R+8]", "\x8B\x44\x80\x01",       4 },
		{ "ADD 8[R],1",      "\x80\0\x01",             3 },
		{ "ADD [R], 32",     "\x81\0\x01\0\0",         6 },
		{ "MOV [R-1],8",     "\xC6\x45\xFF\0",         4 },
		{ "IMUL R,[R+16],32","\x69\x6E\x2D\x02\0\0",   7 },
		{ "NOT [R+R+1]",     "\xF7\x54\0\0x01",        4 },
		{ "TEST [R+R+1],32", "\xF7\x44\0\x01\x01\0\0", 8 },
		{ "MUL [32]",        "\xF7\x25\x12\0\0\0",     6 },
		{ "TEST B[R+8],8",   "\xF6\x45\x08\x01",       4 },
		{ "MUL B[R+8]",      "\xF6\x65\x08",           3 },


	};

	int j,k=0;

	HMODULE kern32;
	PIMAGE_NT_HEADERS nthdr;
	PIMAGE_EXPORT_DIRECTORY imexp;

	DWORD *names, *funcs;
	WORD* ords;
	

	printf("Doing built-in test...\n");

	// run opcode tests
	for (j = 0; j < sizeof(testData)/sizeof(testData[0]); j++) {
		if (run_opcode_test(testData[j].testName, testData[j].codePtr,
		                    testData[j].desiredResult) == false) {
			k++;
		}
	}

	if (k > 0) {
		printf("%d failure%s!",k,k==1?"":"s");
		return 1;
	}

	printf("Testing kernel32's exports...\n");
	kern32 = GetModuleHandle("kernel32");
	
	nthdr = ImageNtHeader(kern32);
	imexp = PIMAGE_EXPORT_DIRECTORY((DWORD)nthdr->OptionalHeader.DataDirectory[0].VirtualAddress + (DWORD)kern32);

	names = (DWORD*)((DWORD)imexp->AddressOfNames+(DWORD)kern32);
	funcs = (DWORD*)((DWORD)imexp->AddressOfFunctions+(DWORD)kern32);
	ords = (WORD*)((DWORD)imexp->AddressOfNameOrdinals+(DWORD)kern32);

	for (j = 0; j < imexp->NumberOfNames; j++) {
		k = run_import_test((const char*)(names[j]+(DWORD)kern32),
						(void*)(funcs[ords[j]]+(DWORD)kern32));

		if (k == false) {
			break;
		}
	}

	return 0;
}