
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

#include <stdatomic.h>

#include "Block_private.h"

/*
 * Supporting routines for libobjc2.
 */

bool
_Block_has_signature(const void *b)
{
	int f;

	if (b == nullptr)
		return false;

	f = atomic_load(&((BlockLiteral *)b)->flags);

	return f & BLOCK_HAS_SIGNATURE;
}

const char *
_Block_signature(const void *b)
{
	const BlockLiteral *aBlock;
	int f;

	if (b == nullptr)
		return nullptr;

	aBlock = (const BlockLiteral *)b;
	f = atomic_load(&aBlock->flags);

	if ((f & BLOCK_HAS_SIGNATURE) == 0)
		return nullptr;

	return f & BLOCK_HAS_COPY_DISPOSE ?
		aBlock->copyDisposeDescriptor->typeEncoding :
		aBlock->descriptor->typeEncoding;
}

bool
_Block_tryRetain(const void *b)
{
	BlockLiteral *aBlock;
	int f;

	if (b == nullptr)
		return false;

	aBlock = (void *)b;
	f = atomic_load(&aBlock->flags);

	while (true) {
		if (f & BLOCK_DEALLOCATING)
			return false;

		if ((f & BLOCK_REFCOUNT_MASK) == BLOCK_REFCOUNT_MASK ||
		    atomic_compare_exchange_strong(&aBlock->flags, &f, f + 2))
			return true;
	}
}

bool
_Block_isDeallocating(const void *b)
{
        int f;

        if (b == nullptr)
                return false;

        f = atomic_load(&((const BlockLiteral *)b)->flags);

        return f & BLOCK_DEALLOCATING;
}

/*
 * Extra compatibility routines.
 */

long
_Block_size(const void *b)
{
	if (b == nullptr)
		return 0;

	return ((const BlockLiteral *)b)->descriptor->blockSize;
}

bool
_Block_use_stret(const void *b)
{
	int f;

	if (b == nullptr)
		return false;

	f = atomic_load(&((const BlockLiteral *)b)->flags);

	return (f & (BLOCK_HAS_SIGNATURE | BLOCK_USE_STRET)) ==
		    (BLOCK_HAS_SIGNATURE | BLOCK_USE_STRET);
}
