#pragma once


/**
 * x86_instruction_length
 *
 * Finds the length of a given instruction.
 *
 * Probably not incredibly useful on its own, but exposed for testing
 * purposes.
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

/**
 * bridge_create
 *
 * Creates a bridge (sometimes called a "trampoline" or a "stub") for
 * calling the original versions of hooked functions without having to
 * temporarily rewrite the hook in memory.
 *
 * @param hookedFunction  A function pointer to the function that will be
 *        hooked. The function must not already be hooked to prevent
 *        recursion.
 *
 * @return A function pointer that can be used to call the unhooked
 *         function. Normal use would typecast this to the appropriate
 *         function pointer.
 */
void* bridge_create (void* unhookedFunction);

/**
 * bridge_destroy
 *
 * Destroys a previously created bridge.
 *
 * @param bridge  The bridge t0 destroy, freeing its resources.
 */

void bridge_destroy (void* bridge);