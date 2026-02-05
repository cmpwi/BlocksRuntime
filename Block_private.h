
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

#pragma once

#if __cplusplus
extern "C" {
#endif

enum BlockRefcountFlags : unsigned short {
	BLOCK_DEALLOCATING = 0x1,
	BLOCK_REFCOUNT_MASK = 0xFFFE
};

enum BlockLiteralFlags : int {
	BLOCK_NEEDS_FREE = 1 << 24,
	BLOCK_HAS_COPY_DISPOSE = 1 << 25,
	BLOCK_IS_GLOBAL = 1 << 28,
	BLOCK_USE_STRET = 1 << 29,
	BLOCK_HAS_SIGNATURE = 1 << 30
};

enum BlockByrefFlags : int {
	BLOCK_BYREF_NEEDS_FREE = 1 << 24,
	BLOCK_BYREF_HAS_COPY_DISPOSE = 1 << 25
};

enum BlockCaptureFlags : int {
	BLOCK_FIELD_IS_OBJECT = 3,
	BLOCK_FIELD_IS_BLOCK = 7,
	BLOCK_FIELD_IS_BYREF = 8,
	BLOCK_FIELD_IS_WEAK = 16,
	BLOCK_BYREF_CALLER = 128
};

typedef struct BlockLiteral {
	void *isa;
	int _Atomic flags;
	int reserved;
	void (*BlockInvoke)(struct BlockLiteral *, ...);
	union {
		struct BlockDescriptor *descriptor;
		struct BlockCopyDisposeDescriptor *copyDisposeDescriptor;
	};
} BlockLiteral;

typedef struct BlockDescriptor {
	unsigned long reserved;
	unsigned long blockSize;
	char *typeEncoding;
	char *extendedLayout;
} BlockDescriptor;

typedef struct BlockCopyDisposeDescriptor {
	unsigned long reserved;
	unsigned long blockSize;
	void (*BlockCopyHelper)(BlockLiteral *, const BlockLiteral *);
	void (*BlockDisposeHelper)(BlockLiteral *);
	char *typeEncoding;
	char *extendedLayout;
} BlockCopyDisposeDescriptor;

typedef struct BlockByref {
	void *isa;
	struct BlockByref *forwarding;
	int _Atomic flags;
	unsigned int size;
} BlockByref;

typedef struct BlockExtendedByref {
	void *isa;
	struct BlockExtendedByref *forwarding;
	int _Atomic flags;
	unsigned int size;
	char const *extendedLayout;
} BlockExtendedByref;

typedef struct BlockCopyDisposeByref {
	void *reserved;
	struct BlockCopyDisposeByref *forwarding;
	int _Atomic flags;
	unsigned int size;
	void (*ByrefCopyHelper)(struct BlockCopyDisposeByref *,
				const struct BlockCopyDisposeByref *);
	void (*ByrefDisposeHelper)(struct BlockCopyDisposeByref *);
} BlockCopyDisposeByref;

typedef struct BlockCopyDisposeExtendedByref {
	void *reserved;
	struct BlockCopyDisposeExtendedByref *forwarding;
	int _Atomic flags;
	unsigned int size;
	void (*ByrefCopyHelper)(struct BlockCopyDisposeExtendedByref *,
				const struct BlockCopyDisposeExtendedByref *);
	void (*ByrefDisposeHelper)(struct BlockCopyDisposeExtendedByref *);
	char const *extendedLayout;
} BlockCopyDisposeExtendedByref;

/*
 * Compatibility structures.
 */

struct Block_descriptor_1 {
	unsigned long reserved;
	unsigned long size;
};

struct Block_descriptor_2 {
	void (*copy)(void *, void const *);
	void (*dispose)(void const *);
};

struct Block_descriptor_3 {
	const char *signature;
	const char *layout;
};

struct Block_layout {
	void *isa;
	volatile int flags;
	int reserved;
	void (*invoke)(void *, ...);
	struct Block_descriptor_1 *descriptor;
};

struct Block_byref {
	void *isa;
	struct Block_byref *forwarding;
	volatile int flags;
	unsigned int size;
};

struct Block_byref_2 {
	void (*byref_keep)(struct Block_byref *, struct Block_byref *);
	void (*byref_destroy)(struct Block_byref *);
};

struct Block_byref_3 {
	const char *layout;
};

typedef struct Block_callbacks_RR {
	unsigned long size;
	void (*retain)(void const *);
	void (*release)(void const *);
	void (*destructInstance)(void const *);
} Block_callbacks_RR;

/*
 * Private routines and symbols.
 */

void
_Block_object_assign(void **, void *, int const);

void
_Block_object_dispose(void *, int const);

void
_Block_use_RR2(Block_callbacks_RR const *);

bool
_Block_has_signature(void const *);

char const *
_Block_signature(void const *);

bool
_Block_tryRetain(void *);

bool
_Block_isDeallocating(void const *);

unsigned long
_Block_size(void const *);

bool
_Block_use_stret(void const *);

extern void *_NSConcreteStackBlock[32];
extern void *_NSConcreteMallocBlock[32];
extern void *_NSConcreteGlobalBlock[32];
/* libobjc2 expects these; they are only used under GC and should go away. */
extern void *_NSConcreteAutoBlock[32];
extern void *_NSConcreteFinalizingBlock[32];

#if __cplusplus
}
#endif
