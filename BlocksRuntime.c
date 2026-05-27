
/*
 * Copyright (c) 2025-2026 Paul Olteanu
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "Block.h"
#include "Block_private.h"

/*
 * Objective-C Class variables.
 */

void *_NSConcreteStackBlock[32] = { 0 };
void *_NSConcreteMallocBlock[32] = { 0 };
void *_NSConcreteGlobalBlock[32] = { 0 };
void *_NSConcreteAutoBlock[32] = { 0 };
void *_NSConcreteFinalizingBlock[32] = { 0 };

/*
 * Function pointers for Objective-C object management routines.
 */

[[clang::always_inline]] static void empty(void const *x) { (void)x; }
static void (*_Block_objc_retain)(void const *) = empty;
static void (*_Block_objc_release)(void const *) = empty;
static void (*_Block_objc_delete_weak_refs)(void const *) = empty;

/*
 * Reference counting routines for Blocks and byrefs.
 */

[[clang::always_inline]]
static inline void
retainFlags(int _Atomic *flags)
{
	int f = atomic_load(flags);

	/* We should probably crash if the reference count becomes saturated. */
	while (true) {
		if ((f & BLOCK_REFCOUNT_MASK) == BLOCK_REFCOUNT_MASK ||
		    atomic_compare_exchange_strong(flags, &f, f + 2))
			return;
	}
}

[[clang::always_inline]]
static inline bool
releaseFlags(int _Atomic *flags)
{
	int f = atomic_load(flags);

	/* We should probably crash if the reference count becomes saturated. */
	while (true) {
		if ((f & BLOCK_REFCOUNT_MASK) == BLOCK_REFCOUNT_MASK ||
		    (f & BLOCK_REFCOUNT_MASK) == 0)
			return false;

		/* On the final reference, set deallocating bit. */
		if ((unsigned short)f == 2) {
			if (atomic_compare_exchange_strong(flags, &f, f - 1))
				return true;
		} else {
			if (atomic_compare_exchange_strong(flags, &f, f - 2))
				return false;
		}
	}
}

/*
 * Public routines for managing Blocks.
 */

void *
_Block_copy(void *b)
{
	BlockLiteral *aBlock, *blockCopy;
	int f;

	aBlock = (BlockLiteral *)b;
	f = atomic_load(&aBlock->flags);

	/* Global Blocks are immutable, just return it. */
	if (f & BLOCK_IS_GLOBAL && aBlock->isa == &_NSConcreteGlobalBlock)
		return aBlock;

	/* Increment reference count for heap-allocated Blocks. */
	if (f & BLOCK_NEEDS_FREE && aBlock->isa == &_NSConcreteMallocBlock) {
		retainFlags(&aBlock->flags);
		return aBlock;
	}

	/* If execution reached here, we must be a stack Block. */
	if (aBlock->isa != &_NSConcreteStackBlock)
		abort();

	/* Make a copy of the stack Block and call its copy helper. */
	blockCopy = malloc(aBlock->descriptor->blockSize);
	memcpy(blockCopy, aBlock, aBlock->descriptor->blockSize);
	blockCopy->isa = _NSConcreteMallocBlock;
	atomic_init(&blockCopy->flags, f | BLOCK_NEEDS_FREE | 2);

	if (f & BLOCK_HAS_COPY_DISPOSE)
		aBlock->copyDisposeDescriptor->BlockCopyHelper(blockCopy,
								aBlock);

	return blockCopy;
}

void
_Block_release(void *b)
{
	BlockLiteral *aBlock;
	int f;

	aBlock = (BlockLiteral *)b;
	f = atomic_load(&aBlock->flags);

	/*
	 * Global Blocks and stack Blocks do not participate in reference
	 * counting. A valid heap Block must have BLOCK_NEEDS_FREE set; its
	 * final reference is signaled when releaseFlags() returns true.
	 */
	if (f & BLOCK_IS_GLOBAL || (f & BLOCK_NEEDS_FREE) == 0 ||
	    aBlock->isa == &_NSConcreteStackBlock ||
	    releaseFlags(&aBlock->flags) == false)
		return;

	/*
	 * If execution reached here, the heap Block is deallocating. Call
	 * our dispose helper.
	 */
	if (f & BLOCK_HAS_COPY_DISPOSE)
		aBlock->copyDisposeDescriptor->BlockDisposeHelper(aBlock);

	/* Tell the Objective-C runtime to remove all weak references to us. */
	_Block_objc_delete_weak_refs(aBlock);
	free(aBlock);
}

/*
 * Private routines for managing byrefs.
 */

[[clang::always_inline]]
static inline void
_Block_byref_assign(void **d, void *s)
{
	BlockCopyDisposeByref *src, *fwd, *copy;
	int f;

	/* Get the real Byref from the forwarding pointer. */
	src = (BlockCopyDisposeByref *)s;
	fwd = src->forwarding;
	copy = fwd;
	f = atomic_load(&fwd->flags);

	/*
	 * Heap Byrefs are reference counted. If given a stack Byref, make a
	 * copy. Give it two references, one for itself and another for the
	 * stack Byref pointing to it. The heap Byref is canonical.
	 */
	if (f & BLOCK_BYREF_NEEDS_FREE)
		retainFlags(&fwd->flags);
	else if ((f & BLOCK_REFCOUNT_MASK) == 0 && src == fwd) {
		copy = malloc(src->size);
		memcpy(copy, src, src->size);
		copy->forwarding = copy;
		atomic_init(&copy->flags, f | BLOCK_BYREF_NEEDS_FREE | 4);
		
		if (f & BLOCK_BYREF_HAS_COPY_DISPOSE)
			src->ByrefCopyHelper(copy, src);

		src->forwarding = copy;
	} else
		abort(); /* Unreachable. */

	*d = (void *)copy;
}

[[clang::always_inline]]
static inline void
_Block_byref_dispose(void const *s)
{
	BlockCopyDisposeByref *fwd;
	int f;

	/* Get the real Byref from the forwarding pointer. */
	fwd = ((BlockCopyDisposeByref const *)s)->forwarding;
	f = atomic_load(&fwd->flags);

	if ((f & BLOCK_BYREF_NEEDS_FREE) == 0 ||
	     releaseFlags(&fwd->flags) == false)
		return;

	if (f & BLOCK_BYREF_HAS_COPY_DISPOSE)
		fwd->ByrefDisposeHelper(fwd);

	free(fwd);
}

/*
 * Compiler-called routines for managing captured Blocks, byrefs, and objects.
 */

void
_Block_object_assign(void **dest, void *src,
			BlockCaptureFlags const captureFlags)
{
	switch (captureFlags) {
	/* Retain the object; assign in dest. */
	case BLOCK_FIELD_IS_OBJECT:
		_Block_objc_retain(src);
		*dest = src;
		return;

	/* Copy stack Block or retain heap Block; assign in dest. */
	case BLOCK_FIELD_IS_BLOCK:
		*dest = _Block_copy(src);
		return;

	/* Copy stack Byref or retain heap Byref; assign in dest. */
	case BLOCK_FIELD_IS_BYREF:
	case BLOCK_FIELD_IS_WEAK_BYREF:
		_Block_byref_assign(dest, src);
		return;

	/*
	 * Blocks and objects in a Byref are managed by the compiler; we only
	 * need to copy pointers.
	 */
	case BLOCK_BYREF_CALLER_FIELD_IS_BLOCK:
	case BLOCK_BYREF_CALLER_FIELD_IS_OBJECT:
	case BLOCK_BYREF_CALLER_FIELD_IS_WEAK_BLOCK:
	case BLOCK_BYREF_CALLER_FIELD_IS_WEAK_OBJECT:
		*dest = src;
		return;
	}

	/* Unreachable. */
	abort();
}

void
_Block_object_dispose(void *src, BlockCaptureFlags const captureFlags)
{
	switch (captureFlags) {
	case BLOCK_FIELD_IS_OBJECT:
		_Block_objc_release(src);
		return;

	case BLOCK_FIELD_IS_BLOCK:
		_Block_release(src);
		return;

	case BLOCK_FIELD_IS_BYREF:
	case BLOCK_FIELD_IS_WEAK_BYREF:
		_Block_byref_dispose(src);
		return;

	/* Blocks and objects in a Byref are managed by the compiler. */
	case BLOCK_BYREF_CALLER_FIELD_IS_BLOCK:
	case BLOCK_BYREF_CALLER_FIELD_IS_OBJECT:
	case BLOCK_BYREF_CALLER_FIELD_IS_WEAK_BLOCK:
	case BLOCK_BYREF_CALLER_FIELD_IS_WEAK_OBJECT:
		return;
	}

	/* Unreachable. */
	abort();
}

/*
 * Callback hooks from libobjc2 for manipulating objects.
 */

void
_Block_use_RR2(Block_callbacks_RR const *callbacks)
{
	_Block_objc_retain = callbacks->retain;
	_Block_objc_release = callbacks->release;
	_Block_objc_delete_weak_refs = callbacks->destructInstance;
}
