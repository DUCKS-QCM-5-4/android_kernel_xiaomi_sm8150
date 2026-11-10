// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) TODO
 * ION legacy heap mask translation.
 *
 * Vendor userspace built against 4.9/4.19 UAPI headers passes heap masks
 * whose bit positions differ from the 5.4 msm_ion_ids.h layout. This file
 * translates such masks before heap selection in ion_buffer_alloc().
 *
 *  Heap             legacy (4.9/4.19)  5.4          class
 *  ---------------  -----------------  -----------  -----------------------
 *  SYSTEM (generic) BIT(0)             BIT(25)      alias
 *  SPSS             BIT(13)            BIT(14)      legacy-only bit
 *  QSECOM_TA        BIT(19)            BIT(1)       legacy-only bit
 *  CAMERA           BIT(20)            BIT(2)       legacy-only bit
 *  ADSP             BIT(22)            BIT(4)       legacy-only bit
 *  USER_CONTIG      BIT(26)            BIT(6)       legacy-only bit
 *  QSECOM           BIT(27)            BIT(7)       legacy-only bit
 *  AUDIO            BIT(28)            BIT(8)       legacy-only bit
 *  CP_MM            BIT(8)             BIT(9)       shared bit (ambiguous)
 *  SECURE           BIT(9)             BIT(10)      shared bit (ambiguous)
 *  SECURE_DISPLAY   BIT(10)            BIT(11)      shared bit (ambiguous)
 *  SECURE_CARVEOUT  BIT(14)            BIT(15)      shared bit (ambiguous)
 *  SYSTEM (msm)     BIT(25)            BIT(25)      unchanged
 *
 * Legacy-only bits are unused in 5.4 and are always translated.
 *
 * Shared bits mean one heap to legacy callers and another to native 5.4
 * callers, so the mask alone cannot decide. Their translation is compiled
 * in only when ION_LEGACY_TRANSLATE_SHARED is defined. When it is, a shared
 * bit is translated only if the 5.4 target heap exists AND either:
 *   - the native meaning of that bit is not a registered heap, or
 *   - the allocation flags identify the legacy meaning and not the native.
 *
 * Whether or not translation is enabled, requests that use a shared bit
 * are logged (ion_shared:) so the owning blobs can be identified.
 *
 * Masks with many bits set are "any heap" wildcards (for example Codec2's
 * software allocator passes ~0) and are never remapped; only the system
 * alias applies to them.
 */

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/msm_ion.h>

#include "ion_legacy.h"

/* dont even bother with shared bits ;-( */
/* #define ION_LEGACY_TRANSLATE_SHARED */

/* Real legacy requests name at most four heaps; more means wildcard. */
#define ION_LEGACY_MAX_BITS	4

#define ION_LEGACY_ONLY_BITS \
	(BIT(13) | BIT(19) | BIT(20) | BIT(22) | BIT(26) | BIT(27) | BIT(28))
#define ION_LEGACY_SHARED_BITS \
	(BIT(8) | BIT(9) | BIT(10) | BIT(14))

#ifdef ION_LEGACY_TRANSLATE_SHARED
#define ION_LEGACY_TRIGGER_BITS \
	(ION_HEAP_SYSTEM | ION_LEGACY_ONLY_BITS | ION_LEGACY_SHARED_BITS)
#else
#define ION_LEGACY_TRIGGER_BITS \
	(ION_HEAP_SYSTEM | ION_LEGACY_ONLY_BITS)
#endif

struct ion_legacy_remap {
	u32 legacy;		/* bit used by 4.9/4.19 userspace */
	u32 native;		/* equivalent bit on 5.4 */
};

static const struct ion_legacy_remap ion_legacy_only[] = {
	{ BIT(13), ION_SPSS_HEAP_ID },
	{ BIT(19), ION_QSECOM_TA_HEAP_ID },
	{ BIT(20), ION_CAMERA_HEAP_ID },
	{ BIT(22), ION_ADSP_HEAP_ID },
	{ BIT(26), ION_USER_CONTIG_HEAP_ID },
	{ BIT(27), ION_QSECOM_HEAP_ID },
	{ BIT(28), ION_AUDIO_HEAP_ID },
};

#ifdef ION_LEGACY_TRANSLATE_SHARED

struct ion_legacy_shared {
	u32 bit;		/* bit position shared by both ABIs */
	u32 target;		/* 5.4 heap the legacy meaning maps to */
	u32 legacy_flags;	/* any of these flags => legacy meaning */
	u32 native_flags;	/* any of these flags => keep native meaning */
};

static const struct ion_legacy_shared ion_legacy_shared[] = {
	/* legacy CP_MM (native: AUDIO) */
	{
		.bit = BIT(8),
		.target = ION_CP_MM_HEAP_ID,
		.legacy_flags = ION_FLAG_SECURE | ION_FLAGS_CP_MASK,
	},
	/* legacy SECURE (native: CP_MM) */
	{
		.bit = BIT(9),
		.target = ION_SECURE_HEAP_ID,
	},
	/* legacy SECURE_DISPLAY (native: SECURE) */
	{
		.bit = BIT(10),
		.target = ION_SECURE_DISPLAY_HEAP_ID,
		.legacy_flags = ION_FLAG_CP_SEC_DISPLAY,
	},
	/* legacy SECURE_CARVEOUT (native: SPSS) */
	{
		.bit = BIT(14),
		.target = ION_SECURE_CARVEOUT_HEAP_ID,
		.legacy_flags = ION_FLAG_CP_CDSP,
		.native_flags = ION_FLAG_CP_SPSS_SP |
				ION_FLAG_CP_SPSS_SP_SHARED |
				ION_FLAG_CP_SPSS_HLOS_SHARED,
	},
};

/*
 * Lockless on purpose: callers may already hold dev->lock, and a recursive
 * down_read() can deadlock behind a queued writer. test_bit() is atomic, and
 * a racing heap removal is benign because the allocation walk under
 * dev->lock fails cleanly for a missing heap.
 */
static inline bool ion_legacy_heap_present(const struct ion_device *dev,
					   u32 heap_bit)
{
	return test_bit(__ffs(heap_bit), dev->heap_ids);
}

static bool ion_legacy_shared_applies(const struct ion_device *dev,
				      const struct ion_legacy_shared *s,
				      u32 flags)
{
	if (!ion_legacy_heap_present(dev, s->target))
		return false;

	if (!ion_legacy_heap_present(dev, s->bit))
		return true;

	return (flags & s->legacy_flags) && !(flags & s->native_flags);
}

#endif /* ION_LEGACY_TRANSLATE_SHARED */

/**
 * ion_translate_legacy_heap_mask - translate a legacy heap mask to 5.4 layout
 * @dev:      ION device (heap registration is used to disambiguate)
 * @old_mask: heap_id_mask as supplied by the caller
 * @flags:    allocation flags supplied by the caller
 *
 * Return: the mask to use for heap selection. Equal to @old_mask when no
 * translation is needed.
 */
unsigned int ion_translate_legacy_heap_mask(struct ion_device *dev,
					    unsigned int old_mask,
					    unsigned int flags)
{
	u32 remove = 0, add = 0, new_mask;
	size_t i;

	/* Generic system heap aliases to the MSM system heap (old blobs may need it). */
	if (old_mask & ION_HEAP_SYSTEM) {
		remove |= ION_HEAP_SYSTEM;
		add |= ION_SYSTEM_HEAP_ID;
	}

	/*
	 * "any heap" requests are not heap-specific, so skip
	 * legacy remaps,
	 */
	if (hweight32(old_mask) > ION_LEGACY_MAX_BITS) {
		new_mask = (old_mask & ~remove) | add;
		goto out;
	}

	/* Detector: identify callers that use ambiguous shared bits. */
	if (unlikely(old_mask & ION_LEGACY_SHARED_BITS))
		pr_err_ratelimited("ion_shared: %s[%d] mask 0x%08x flags 0x%08x\n",
				    current->comm, task_pid_nr(current),
				    old_mask, flags);

	/* Fast path for native 5.4 callers. */
	if (likely(!(old_mask & ION_LEGACY_TRIGGER_BITS)))
		return old_mask;

	for (i = 0; i < ARRAY_SIZE(ion_legacy_only); i++) {
		if (old_mask & ion_legacy_only[i].legacy) {
			remove |= ion_legacy_only[i].legacy;
			add |= ion_legacy_only[i].native;
		}
	}

#ifdef ION_LEGACY_TRANSLATE_SHARED
	for (i = 0; i < ARRAY_SIZE(ion_legacy_shared); i++) {
		const struct ion_legacy_shared *s = &ion_legacy_shared[i];

		if ((old_mask & s->bit) &&
		    ion_legacy_shared_applies(dev, s, flags)) {
			remove |= s->bit;
			add |= s->target;
		}
	}
#endif

	/*
	 * Compute from the original mask, not incrementally, so overlapping
	 * moves cannot clobber each other.
	 */
	new_mask = (old_mask & ~remove) | add;
out:
	if (new_mask != old_mask)
		pr_warn_ratelimited("ion_legacy: %s[%d]: heap mask 0x%08x to 0x%08x (flags 0x%08x)\n",
				    current->comm, task_pid_nr(current),
				    old_mask, new_mask, flags);

	return new_mask;
}