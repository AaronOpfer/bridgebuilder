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
	bool hasSIB = false;

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
		hasSIB = true;
	}
	/*
	// if MOD is 0,1 or 2, and R/M = ESP, then we have SIB
	else if ((cPtr[1] >> 6) < 3 && (cPtr[1] & 7) == 5) {
		length++;
		hasSIB = true;
	}*/

	// if MOD==0 and base==0b101, then we have displacement!
	if (hasSIB && (cPtr[1] >> 6) == 0 && (cPtr[2]&7) == 5) {
		length += 4;
	}

	return length;
}

int x86_instruction_length (void* codePtr, bool stopOnUnrelocateable) {
	bool operandSizePrefix = false, addressSizePrefix = false;

	int length = 0, j;

	char operandSize = 4, addressSize = 4;

	// cast the void pointer to char to make type conversions easier
	unsigned char* cPtr = (unsigned char*)codePtr;

	// iterate through bytes until we find one that isn't a prefix
	for (j = 0; ; j++) {
		switch (cPtr[j]) {
			case 0x66:
				operandSizePrefix = true;
				operandSize = 2;
				length++;
				continue;
			case 0x67:
				addressSizePrefix = true;
				addressSize = 2;
				length++;
				continue;
			case 0x2E: case 0x36: case 0x3E:
			case 0x26: case 0x64: case 0x65:
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

	// handle static-sized opcodes
	switch (*cPtr) {
		//-----------------------------------------
		// single-byte instructions
		//-----------------------------------------

		// 1-byte INC instructions
		case 0x40: case 0x41: case 0x42: case 0x43: 
		case 0x44: case 0x45: case 0x46: case 0x47:
		// 1-byte DEC instructions
		case 0x48: case 0x49: case 0x4A: case 0x4B:
		case 0x4C: case 0x4D: case 0x4E: case 0x4F:
		// 1-byte PUSH instructions
		case 0x06: case 0x0E: case 0x16: case 0x1E:
		case 0x50: case 0x51: case 0x52: case 0x53: 
		case 0x54: case 0x55: case 0x56: case 0x57:
		// 1-byte POP instructions
		case 0x07: case 0x17: case 0x1F:
		case 0x58: case 0x59: case 0x5A: case 0x5B:
		case 0x5C: case 0x5D: case 0x5E: case 0x5F:
		// INS/OUTS
		case 0x6C: case 0x6D: case 0x6E: case 0x6F:
		// RETF, RETN
		case 0xC3: case 0xCB:
		// NOP
		case 0x90:
		// XCHG AX, reg16
		case 0x91: case 0x92: case 0x93: case 0x94:
		case 0x95: case 0x96: case 0x97:
		// CBW & CWDE, CDQ & CWD
		case 0x98: case 0x99:
		// PUSH[AF] and POP[AF]
		case 0x60: case 0x61: case 0x9C: case 0x9D:
		// [AD]A[AS]
		case 0x27: case 0x2F: case 0x37: case 0x3F:
		// {MOV,CMP}S[BWD]
		case 0xA4: case 0xA5: case 0xA6: case 0xA7:
		// {STO,LOD,SCA}S[BWD]
		case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE:
		// CMC, CLC, STC, CLD, STD
		case 0xF5: case 0xF8: case 0xF9: case 0xFC: case 0xFD:
		// INT1, INT3
		case 0xF1: case 0xCC:
		// XLAT
		case 0xD7:
			length +=1;
			break;

		//-----------------------------------------
		// two-byte instructions, single opcode
		//-----------------------------------------

		// AL w/ imm8
		case 0x04: case 0x0C: case 0x14: case 0x1C:
		case 0x24: case 0x2C: case 0x34: case 0x3C:
		case 0xA8:
		// MOV reg8, imm8
		case 0xB0: case 0xB1: case 0xB2: case 0xB3:
		case 0xB4: case 0xB5: case 0xB6: case 0xB7:
		// PUSH imm8
		case 0x6A:
			length += 2;
			break;

		//-----------------------------------------
		// three-byte instructions
		//-----------------------------------------

		// RET[N/F] imm16
		case 0xC2: case 0xCA:
			length += 3;
			break;

		//-----------------------------------------
		// five-byte instructions
		//-----------------------------------------

		// PUSH imm32
		case 0x68:
			length += 5;
			break;

		//-----------------------------------------
		// 0F instructions
		//-----------------------------------------
		case 0x0F:
			switch (cPtr[1]) {
				// MOD-REG-R/M instructions
				case 0xB6: case 0xB7: case 0xBE: case 0xBF:
				// conditional SET instructions
				case 0x90: case 0x91: case 0x92: case 0x93:
				case 0x94: case 0x95: case 0x96: case 0x97:
				case 0x98: case 0x99: case 0x9A: case 0x9B:
				case 0x9C: case 0x9D: case 0x9E: case 0x9F:
					length += 1 + x86_instruction_length_mod_reg_rm(&cPtr[1]);
					break;
				// conditional far jumps. Unrelocateable!
				case 0x80: case 0x81: case 0x82: case 0x83:
				case 0x84: case 0x85: case 0x86: case 0x87:
				case 0x88: case 0x89: case 0x8A: case 0x8B:
				case 0x8C: case 0x8D: case 0x8E: case 0x8F:
					if (stopOnUnrelocateable == true) {
						return -2;
					}
					length += 6;
					break;

				default:
					printf("Opcode 0F %02X @ 0x%08X = ???\n", cPtr[1],cPtr);
					return -1;
			}
			break;

		//-----------------------------------------
		// variable-length instructions
		//-----------------------------------------

		// operations on [E]AX with operand imm{16,32}
		case 0x05: case 0x0D: case 0x15: case 0x1D:
		case 0x25: case 0x2D: case 0x36: case 0x3D:
		case 0xA9:
		// MOV reg16/reg32, imm16/mm32
		case 0xB8: case 0xB9: case 0xBA: case 0xBB:
		case 0xBC: case 0xBD: case 0xBE: case 0xBF:
			length += 1 + operandSize;
			break;

		// MOV [E]AX, ptr [] (either direction)
		case 0xA0: case 0xA1: case 0xA2: case 0xA3:
			length += 1 + addressSize;
			break;

		// MOD-REG-R/M operations
		case 0x00: case 0x01: case 0x02: case 0x03: // ADD
		case 0x08: case 0x09: case 0x0A: case 0x0B: // OR
		case 0x10: case 0x11: case 0x12: case 0x13: // ADC
		case 0x18: case 0x19: case 0x1A: case 0x1B: // SBB
		case 0x20: case 0x21: case 0x22: case 0x23: // AND
		case 0x28: case 0x29: case 0x2A: case 0x2B: // SUB
		case 0x30: case 0x31: case 0x32: case 0x33: // XOR
		case 0x38: case 0x39: case 0x3A: case 0x3B: // CMP
		case 0x84: case 0x85: case 0x86: case 0x87: // TEST, XCHG
		case 0x88: case 0x89: case 0x8A: case 0x8B: // MOV
		case 0x62: // BOUND
		case 0x63: // ARPL ?
		case 0xFF: // opcode extension
		case 0x8D: // LEA ?
			length += x86_instruction_length_mod_reg_rm(cPtr);
			break;

		// opcode extensions w/ imm8
		case 0x80: case 0x82: case 0x83: case 0xC0:
		case 0xC1: case 0xC6: case 0x6B:
			length += 1 + x86_instruction_length_mod_reg_rm(cPtr);
			break;
		// opcode extensions w/ imm16/32
		case 0x81: case 0xC7: case 0x69:
			length += operandSize + x86_instruction_length_mod_reg_rm(cPtr);
			break;

		// opcode extension w/ variable arguments
		case 0xF6: case 0xF7:
			length += x86_instruction_length_mod_reg_rm(cPtr);
			if (!(cPtr[1]&0x30)) {
				// F6 is always 8bit, F7 is 16 or 32
				if (*cPtr == 0xF6) {
					length += 1;
				} else {
					length += operandSize;
				}
			}
			break;

			

		//-----------------------------------------
		// unrelocateable instructions
		//-----------------------------------------

		// conditional short jumps
		case 0x70: case 0x71: case 0x72: case 0x73:
		case 0x74: case 0x75: case 0x76: case 0x77:
		case 0x78: case 0x79: case 0x7A: case 0x7B:
		case 0x7C: case 0x7D: case 0x7E: case 0x7F:
		case 0xE3: // wtf is JECXZ?
		// unconditional short jump
		case 0xEB:
			if (stopOnUnrelocateable == true) {
				return -2;
			}
			length += 2;
			break;
		
		// far CALL and JMP
		case 0xE8: case 0xE9:
			if (stopOnUnrelocateable == true) {
				return -2;
			}
			length += 5;
			break;

		//-----------------------------------------
		// failure
		//-----------------------------------------
		default:
			printf("Opcode %02X @ 0x%08X = ???\n", *cPtr,cPtr);
			return -1;
	}

	return length;
}