#include <stdio.h>
#include <string.h>
#include "bridgebuilder.h"
#include "mem/codepool.h"

void* bridge_create (void* unhookedFunction) {
	#ifdef _WIN32
		static const unsigned char msPrologueSignature[] = { 0x8B,0xFF,0x55,0x8B,0xEC,0x5D };
	#endif

	int operatorSize, instructionBytes = 0, bridgeSize = 5;
	bool noRewrites = true;

	unsigned char* bridge;

	unsigned char jmpBytes[] = { 0xE9, 0, 0, 0, 0 };
	unsigned char* codePtr = (unsigned char*)unhookedFunction;


	// Windows-specific: detect Microsoft-specific do-nothing
	// prologue code, and simply return the address after it. This
	// will save a small amount of memory and computation.
	#ifdef _WIN32
	

	if (memcmp(unhookedFunction, msPrologueSignature, 
	           sizeof(msPrologueSignature)) == 0) {
		return (void*)(size_t(unhookedFunction)+6);
	}
	#endif

	// Determine how much memory we need to allocate ahead of time
	while (instructionBytes < 5) {
		operatorSize = x86_instruction_length(&codePtr[instructionBytes],true);
		if (operatorSize == -1) {
			// we failed to make the bridge, bail out!
			return 0;
		}
		// we will have to relocate a jump
		if (operatorSize == -2) {
			noRewrites = false;
			instructionBytes += x86_instruction_length(&codePtr[instructionBytes],false);
			// TODO: write relative jmp/call rebase sizing
			// functionality
			return 0;
		} else {
			instructionBytes += operatorSize;
			bridgeSize += operatorSize;
		}
	}

	// now that we know how much memory we'll need to consume, we can
	// use a slice of our shared memory page and write out the hook
	// function.
	bridge = (unsigned char*)codepool_alloc(bridgeSize);
	if (!bridge) {
		return 0;
	}

	// calculate our JMP function, taking into consideration
	// the possibility that our re-encoded bridge could be
	// longer than the original prologue due to relative
	// instruction rewriting
	*(unsigned long*)(&jmpBytes[1]) =  (unsigned long)unhookedFunction - (unsigned long)bridge - (bridgeSize - instructionBytes) - 5;

	if (noRewrites == true) {
		// we don't have to rebase anything, so we can do a niave copy.
		codepool_unlock(bridge);
		// copy in most of the code
		memcpy(bridge,unhookedFunction,instructionBytes);
		// and then write the JMP
		memcpy(&bridge[instructionBytes],jmpBytes,5);
		// relock the memory
		codepool_lock(bridge);
	}
	return bridge;
}

void bridge_destroy (void* bridge) {
	codepool_free(bridge);
}

int x86_instruction_length_mod_reg_rm (unsigned char* cPtr) {
	// we're adding up the length a few bytes at a time,
	// starting with the opcode and MOD-REG-R/M
	int length = 2;

	// MOD of the MOD-REG-R/M byte
	switch (cPtr[1] >> 6) {
		case 1:
			length += 1; // 1-byte displacement
			break;
		case 2:
			length += 4; // 4-byte displacement
			break;
		case 3:
			return length;
	}

	// displacement only addressing mode
	if ((cPtr[1] >> 6) == 0 && (cPtr[1] & 7) == 5) {
		length += 4;
	}

	// SIB with no displacement
	else if ((cPtr[1] & 7) == 4) {
		length++;
		// if MOD==0 and base==0b101, then we have displacement!
		if ((cPtr[1] >> 6) == 0 && (cPtr[2]&7) == 5) {
			length += 4;
		}
	}



	return length;
}

int x86_instruction_length (void* codePtr, bool stopOnUnrelocateable) {
	int length = 0, j;

	char operandSize = 4, addressSize = 4;

	// cast the void pointer to char to make type conversions easier
	unsigned char* cPtr = (unsigned char*)codePtr;

	// iterate through bytes until we find one that isn't a prefix
	for (j = 0; ; j++) {
		switch (cPtr[j]) {
			case 0x66:
				operandSize = 2;
				length++;
				continue;
			case 0x67:
				addressSize = 2;
				length++;
				continue;
			case 0x26: case 0x2E:
			case 0x36: case 0x3E:
			case 0x64: case 0x65:
				length++;
				continue;
				// prefix group 1
			case 0xF0: case 0xF2: case 0xF3:
				length++;
				continue;
		}
		break;
	}

	// move our pointer ahead
	cPtr = &cPtr[j];


	// handle 0x0F opcode set
	if (*cPtr == 0x0F) {
		// move our pointer ahead again
		cPtr = &cPtr[1];

		if (   (*cPtr & 0xF0) == 0x90 // 90-9F
		    || (*cPtr & 0xF6) == 0xB6 // B6,B7,BE,BF
		   ) {
			return length+1+x86_instruction_length_mod_reg_rm(cPtr);
		}

		if ((*cPtr & 0xF0) == 0x80) {
			if (stopOnUnrelocateable == true) {
				return -2;
			}
			return length+6;
		}

		printf("Opcode 0F %02X @ 0x%08X = ???\n", *cPtr,&cPtr[-1]);
		return -1;
	}

	// FIXME: We don't properly decode FAR CALL 0x9A, but
	// is it ever used in the wild?


	// These opcodes refuse to be matchable with bitmasks
	switch(*cPtr) {
		case 0xC3: case 0xD7:
			return length+1;
		case 0xA8: case 0x6A:
			return length+2;
		case 0xC8:
			return length+4;
		case 0x68:
			return length+5;
		// opcode extensions w/ imm16/32
		case 0x69:
			return length + operandSize + x86_instruction_length_mod_reg_rm(cPtr);
			break;
		// opcode extensions w/ imm8
		case 0x6B:
			return length + 1 + x86_instruction_length_mod_reg_rm(cPtr);
			break;
	}
		

	//* experimental bit-matching for 1byte opcodes
	if (   (*cPtr & 0xC6) == 6 // 06,07,0E,0F...36,37,3E,3F
	    || (*cPtr & 0xE0) == 0x40 // 0x40-0x5F
	    || (*cPtr & 0xFE) == 0x60 // 60-61
	    || (*cPtr & 0x7C) == 0x6C // 6C-6F, EC-EF
	    || (*cPtr & 0xF0) == 0x90 // 90 - 9F
	    || (*cPtr & 0xF4) == 0xA4 // A4-A7,AC-AF
	    || (*cPtr & 0xFE) == 0xAA // AA,AB
	    || (*cPtr & 0xFD) == 0xC9 // C9, CB
	    || (*cPtr & 0xFD) == 0xCC // CC, CE
	    || (*cPtr & 0xF4) == 0xF0 // F0-F3,F8-FB
	    || (*cPtr & 0xF6) == 0xF4 // F4-F5,FC-FD
	   ) {
		return length+1;
	}

	// relative 2 byte JMPs
	if (   (*cPtr & 0xF0) == 0x70 // 70-7F
	    || (*cPtr & 0xF7) == 0xE3 // E3, EB
	   ) {
		if (stopOnUnrelocateable == true) {
			return -2;
		}
		return length+2;
	}

	//* experimental bit-matching for 2byte opcodes
	if (   (*cPtr & 0xC7) == 4    // 4,C,14,1C,24...34,3C
	    || (*cPtr & 0xF8) == 0xB0 // B0-B7
	    || (*cPtr & 0xFD) == 0xCD // CD, CF
	    || (*cPtr & 0xFD) == 0xD0 // D0, D2
	    || (*cPtr & 0xF8) == 0xE0 // E0-E7
	   ) {
		return length+2;
	}


	//* experimental bit-matching for 3byte opcodes
	if ((*cPtr & 0xF7) == 0xC2) { // C2,CA
		return length+3;
	}

	// MOD-REG-RM
	if (   (*cPtr & 0xC4) == 0 // 00-03,08-0B,...30-33,38-3B
	    || (*cPtr & 0xFE) == 0x62 // 62,53
	    || (*cPtr & 0xFC) == 0x84 // 84-87
	    || (*cPtr & 0xF8) == 0x88 // 88-8F
	    || (*cPtr & 0xFE) == 0xFE // FE-FF
	  ) {
		return length+x86_instruction_length_mod_reg_rm(cPtr);
	}

	// 1 + MOD-REG-RM
	if (   (*cPtr & 0xFE) == 0x82 // 82, 83
	    || (*cPtr & 0xFE) == 0xC0 // C0, C1
	   ) {
		return length+1+x86_instruction_length_mod_reg_rm(cPtr);
	}

	// M+1 and M+operandsize placed consecutively
	if (   (*cPtr & 0xFE) == 0x80 // 80, 81
	    || (*cPtr & 0xFE) == 0xC6 // C6, C7
	   ) {
		return length
		       + x86_instruction_length_mod_reg_rm(cPtr)
		       + (((*cPtr&1)==1) ? operandSize : 1);
	}

	// 1 + address size
	if (   (*cPtr & 0xFC) == 0xA0 // A0-A3
	    || (*cPtr & 0xF7) == 0x35 // 35, 3D.
	   ) {
		return length+1+addressSize;
	}

	// 1 + operand size
	if (   (*cPtr & 0xF8) == 0xB8 // B8-BF
	    || (*cPtr & 0xC7) == 0x05 // MUST be after evaluating 35 & 3D.
	    || *cPtr == 0xA9
        ) {
		return length+1+operandSize;
	}

	// bizarre opcode extension stuff
	if ((*cPtr & 0xFE) == 0xF6) { // F6, F7
		// opcode extension w/ variable arguments
		length += x86_instruction_length_mod_reg_rm(cPtr);
		if (!(cPtr[1]&0x30)) {
			// F6 is always 8bit, F7 is 16 or 32
			if (*cPtr == 0xF6) {
				length += 1;
			} else {
				length += operandSize;
			}
		}
		return length;
	}

	if ((*cPtr & 0xFE) == 0xE8) { //E8,E9
		if (stopOnUnrelocateable == true) {
			return -2;
		}
		return length+5;
	}


	printf("Opcode %02X @ 0x%08X = ???\n", *cPtr,cPtr);
	return -1;

	return length;
}