
/*
 * Copyright (c) 2025 Paul Olteanu
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

#include "Block_private.h"

/*
 * Objective-C Class variables.
 */

void *_NSConcreteStackBlock[32] = { 0 };
void *_NSConcreteMallocBlock[32] = { 0 };
void *_NSConcreteGlobalBlock[32] = { 0 };
/* libobjc2 expects these; they are only used under GC and should go away. */
void *_NSConcreteAutoBlock[32] = { 0 };
void *_NSConcreteFinalizingBlock[32] = { 0 };

/*
 * Function pointers for Objective-C object management routines.
 */

[[clang::always_inline]] static void empty(const void *x) { };
static void (*_Block_objc_retain)(const void *) = empty;
static void (*_Block_objc_release)(const void *) = empty;
static void (*_Block_objc_delete_weak_refs)(const void *) = empty;

/*
 * Reference counting routines for Blocks and byrefs.
 */

[[clang::always_inline]]
inline static void
retainFlags(_Atomic int *flags)
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
releaseFlags(_Atomic int *flags)
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
_Block_copy(const void *b)
{
	BlockLiteral *aBlock, *blockCopy;
	int f;

	if (b == nullptr)
		return nullptr;

	aBlock = (void *)b;
	f = atomic_load(&aBlock->flags);

	if (f & BLOCK_IS_GLOBAL && aBlock->isa == &_NSConcreteGlobalBlock)
		return aBlock;

	if (f & BLOCK_NEEDS_FREE && aBlock->isa == &_NSConcreteMallocBlock) {
		retainFlags(&aBlock->flags);
		return aBlock;
	}

	if (aBlock->isa != &_NSConcreteStackBlock ||
	    (blockCopy = calloc(1, aBlock->descriptor->blockSize)) == nullptr)
		return nullptr;

	memmove(blockCopy, aBlock, aBlock->descriptor->blockSize);
	blockCopy->isa = _NSConcreteMallocBlock;
	atomic_init(&blockCopy->flags, f | BLOCK_NEEDS_FREE | 2);

	if (f & BLOCK_HAS_COPY_DISPOSE)
		(*aBlock->copyDisposeDescriptor->BlockCopyHelper)(blockCopy,
								aBlock);

	return blockCopy;
}

void
_Block_release(const void *b)
{
	BlockLiteral *aBlock;
	int f;

	if (b == nullptr)
		return;

	aBlock = (void *)b;
	f = atomic_load(&aBlock->flags);

	if (f & BLOCK_IS_GLOBAL || (f & BLOCK_NEEDS_FREE) == 0 ||
	    aBlock->isa == &_NSConcreteStackBlock ||
	    releaseFlags(&aBlock->flags) == false)
		return;

	if (f & BLOCK_HAS_COPY_DISPOSE)
		(*aBlock->copyDisposeDescriptor->BlockDisposeHelper)(aBlock);

	_Block_objc_delete_weak_refs(aBlock);
	_Block_objc_release(aBlock);
	free(aBlock);
}

/*
 * Private routines for managing byrefs.
 */

[[clang::always_inline]]
inline static void
_Block_byref_assign(void *d, const void *s)
{
	BlockCopyDisposeByref *src, *fwd, *expected, *copy;
	int f;

	if (d == nullptr || s == nullptr)
		return;

	src = (BlockCopyDisposeByref *)s;
	fwd = atomic_load_explicit(&src->forwarding, memory_order_acquire);
	expected = src;

	f = atomic_load_explicit(&fwd->flags, memory_order_relaxed);

	if (f & BLOCK_BYREF_NEEDS_FREE)
		retainFlags(&fwd->flags);
	else if ((f & BLOCK_REFCOUNT_MASK) == 0 && src == fwd) {
		if ((copy = calloc(1, src->size)) == nullptr)
			return;

		memmove(copy, src, src->size);
		atomic_init(&copy->forwarding, copy);
		atomic_init(&copy->flags, f | BLOCK_BYREF_NEEDS_FREE | 4);

		if (atomic_compare_exchange_strong_explicit(&src->forwarding,
		    &expected, copy, memory_order_release,
		    memory_order_relaxed)) {
			fwd = copy;
			if (f & BLOCK_BYREF_HAS_COPY_DISPOSE)
				(*src->ByrefCopyHelper)(copy, src);
		} else
			free((void *)copy);
	}

	*(void **)d = fwd;
}

[[clang::always_inline]]
inline static void
_Block_byref_dispose(const void *s)
{
	BlockCopyDisposeByref *src, *fwd;
	int f;

	if (s == nullptr)
		return;

	src = (BlockCopyDisposeByref *)s;
	fwd = atomic_load_explicit(&src->forwarding, memory_order_acquire);
	f = atomic_load_explicit(&fwd->flags, memory_order_relaxed);

	if ((f & BLOCK_BYREF_NEEDS_FREE) == 0 ||
	    (f & BLOCK_REFCOUNT_MASK) == 0)
		return;

	if (releaseFlags(&fwd->flags)) {
		if (f & BLOCK_BYREF_HAS_COPY_DISPOSE)
			(*fwd->ByrefDisposeHelper)(fwd);

		free((void *)fwd);
	}
}

/*
 * Compiler-called routines for managing captured Blocks, byrefs, and objects.
 */

void
_Block_object_assign(void *dest, const void *src, const int captureFlags)
{	
	if ((captureFlags & (BLOCK_FIELD_IS_OBJECT | BLOCK_FIELD_IS_BLOCK |
			     BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK |
			     BLOCK_BYREF_CALLER)) == 0)
		return;

	switch (captureFlags) {
	case BLOCK_FIELD_IS_OBJECT:
		_Block_objc_retain(src);
		*(const void **)dest = src;
		break;

	case BLOCK_FIELD_IS_BLOCK:
		*(void **)dest = _Block_copy((void *)src);
		break;

	case BLOCK_FIELD_IS_BYREF:
	case BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK:
		_Block_byref_assign(dest, src);
		break;

	case BLOCK_FIELD_IS_BLOCK | BLOCK_BYREF_CALLER:
	case BLOCK_FIELD_IS_BYREF | BLOCK_BYREF_CALLER:
	case BLOCK_FIELD_IS_BLOCK | BLOCK_FIELD_IS_WEAK | BLOCK_BYREF_CALLER:
	case BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK | BLOCK_BYREF_CALLER:
		*(const void **)dest = src;
		break;

	default:
		break;
	}
}

void
_Block_object_dispose(const void *src, const int captureFlags)
{
	if ((captureFlags & (BLOCK_FIELD_IS_OBJECT | BLOCK_FIELD_IS_BLOCK |
			     BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK |
			     BLOCK_BYREF_CALLER)) == 0)
		return;

	switch (captureFlags) {
	case BLOCK_FIELD_IS_OBJECT:
		_Block_objc_release(src);
		break;

	case BLOCK_FIELD_IS_BLOCK:
		_Block_release((void *)src);
		break;

	case BLOCK_FIELD_IS_BYREF:
	case BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK:
		_Block_byref_dispose(src);
		break;

	default:
		break;
	}
}

/*
 * Callback hooks from libobjc2 for manipulating Blocks as objects.
 */

void
_Block_use_RR2(const Block_callbacks_RR *callbacks)
{
        _Block_objc_retain = callbacks->retain;
        _Block_objc_release = callbacks->release;
        _Block_objc_delete_weak_refs = callbacks->destructInstance;
}
