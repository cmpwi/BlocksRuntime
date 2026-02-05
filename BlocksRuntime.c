
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
inline static void
retainFlags(int _Atomic *flags)
{
	int f = atomic_load(flags);

	while (true) {
		if ((f & BLOCK_REFCOUNT_MASK) == BLOCK_REFCOUNT_MASK ||
		    atomic_compare_exchange_strong(flags, &f, f + 2))
			return;
	}
}

[[clang::always_inline]]
inline static bool
releaseFlags(int _Atomic *flags)
{
	int f = atomic_load(flags);

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

	if (f & BLOCK_IS_GLOBAL && aBlock->isa == &_NSConcreteGlobalBlock)
		return aBlock;

	if (f & BLOCK_NEEDS_FREE && aBlock->isa == &_NSConcreteMallocBlock) {
		retainFlags(&aBlock->flags);
		return aBlock;
	}

	if (aBlock->isa != &_NSConcreteStackBlock ||
	    (blockCopy = (BlockLiteral *)malloc(
	     aBlock->descriptor->blockSize)) == nullptr)
		abort();

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

	if (f & BLOCK_IS_GLOBAL || (f & BLOCK_NEEDS_FREE) == 0 ||
	    aBlock->isa == &_NSConcreteStackBlock ||
	    releaseFlags(&aBlock->flags) == false)
		return;

	if (f & BLOCK_HAS_COPY_DISPOSE)
		aBlock->copyDisposeDescriptor->BlockDisposeHelper(aBlock);

	_Block_objc_delete_weak_refs(aBlock);
	_Block_objc_release(aBlock);
	free((void *)aBlock);
}

/*
 * Private routines for managing byrefs.
 */

[[clang::always_inline]]
inline static void
_Block_byref_assign(void **d, void *s)
{
	BlockCopyDisposeByref *src, *fwd, *copy;
	int f;

	src = (BlockCopyDisposeByref *)s;
	fwd = src->forwarding;
	copy = fwd;
	f = atomic_load(&fwd->flags);

	if (f & BLOCK_BYREF_NEEDS_FREE)
		retainFlags(&fwd->flags);
	else if ((f & BLOCK_REFCOUNT_MASK) == 0 && src == fwd) {
		if ((copy = calloc(1, src->size)) == nullptr)
			abort();

		memcpy(copy, src, src->size);
		copy->forwarding = copy;
		atomic_init(&copy->flags, f | BLOCK_BYREF_NEEDS_FREE | 4);
		
		if (f & BLOCK_HAS_COPY_DISPOSE)
			src->ByrefCopyHelper(copy, src);

		src->forwarding = copy;
	} else
		abort();

	*d = (void *)copy;
}

[[clang::always_inline]]
inline static void
_Block_byref_dispose(void const *s)
{
	BlockCopyDisposeByref *fwd;
	int f;

	fwd = ((BlockCopyDisposeByref const *)s)->forwarding;
	f = atomic_load(&fwd->flags);

	if ((f & BLOCK_BYREF_NEEDS_FREE) == 0 ||
	    (f & BLOCK_REFCOUNT_MASK) == 0 ||
	     releaseFlags(&fwd->flags) == false)
		return;

	if (f & BLOCK_BYREF_HAS_COPY_DISPOSE)
		fwd->ByrefDisposeHelper(fwd);

	free((void *)fwd);
}

/*
 * Compiler-called routines for managing captured Blocks, byrefs, and objects.
 */

void
_Block_object_assign(void **dest, void *src, int const captureFlags)
{	
	switch (captureFlags) {
	case BLOCK_FIELD_IS_OBJECT:
		_Block_objc_retain(src);
		*dest = src;
		break;

	case BLOCK_FIELD_IS_BLOCK:
		*dest = _Block_copy(src);
		break;

	case BLOCK_FIELD_IS_BYREF:
	case BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK:
		_Block_byref_assign(dest, src);
		break;

	case BLOCK_FIELD_IS_BLOCK | BLOCK_BYREF_CALLER:
	case BLOCK_FIELD_IS_BYREF | BLOCK_BYREF_CALLER:
	case BLOCK_FIELD_IS_BLOCK | BLOCK_FIELD_IS_WEAK | BLOCK_BYREF_CALLER:
	case BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK | BLOCK_BYREF_CALLER:
		*dest = src;
		break;

	default:
		abort();
	}
}

void
_Block_object_dispose(void *src, int const captureFlags)
{
	switch (captureFlags) {
	case BLOCK_FIELD_IS_OBJECT:
		_Block_objc_release(src);
		break;

	case BLOCK_FIELD_IS_BLOCK:
		_Block_release(src);
		break;

	case BLOCK_FIELD_IS_BYREF:
	case BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK:
		_Block_byref_dispose(src);
		break;

	default:
		abort();
	}
}

/*
 * Callback hooks from libobjc2 for manipulating Blocks as objects.
 */

void
_Block_use_RR2(Block_callbacks_RR const *callbacks)
{
        _Block_objc_retain = callbacks->retain;
        _Block_objc_release = callbacks->release;
        _Block_objc_delete_weak_refs = callbacks->destructInstance;
}
