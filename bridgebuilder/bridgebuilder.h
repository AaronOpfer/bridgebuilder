#pragma once


/**
 * x86_instruction_length
 *
 * Finds the length of a given instruction.
 *
 * @param codePtr  A pointer to intel assembly code (function pointer)
 * @param stopOnUnrelocateable If True, the function will not return
 *                             the length of instructions that cannot
 *                             by trivially relocated, instead returning
 *                             -2.
 *
 * @return Length of the instruction. -1 indicates an opcode that was not
 *         properly understood. -2 indicates an instruction that cannot be
 *         trivially relocated if stopOnUnrelocateable is true.
 */
int x86_instruction_length (void* codePtr, bool stopOnUnrelocateable);