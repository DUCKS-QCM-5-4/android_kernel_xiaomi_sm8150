// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) TODO
 *
 * ION legacy userspace heap mask compatibility translation layer.
 * Remaps Android 9/10 (kernel 4.9/4.19) vendor blob ION allocations
 * to 5.4 compact heap IDs for SM8150 and SDM845 platforms.
 *
 * ==============================================================================================================
 *                                   ION HEAP BITMAP TRANSLATION TABLE (4.19/4.9 -> 5.4)
 * ==============================================================================================================
 * Heap Name       | 4.19 Enum / Define           | 4.19 Mask       | 5.4 Define            | 5.4 Mask        | Disambiguation Rule
 * ----------------|------------------------------|-----------------|-----------------------|-----------------|-------------------------------------------------
 * QSECOM_TA       | ION_QSECOM_TA_HEAP_ID (19)   | BIT(19) 0x80000 | ION_QSECOM_TA_HEAP_ID | BIT(1)  0x02    | 4.19-exclusive bit. Direct remap 19 -> 1.
 * CAMERA          | ION_CAMERA_HEAP_ID (20)      | BIT(20) 0x100000| ION_CAMERA_HEAP_ID    | BIT(2)  0x04    | 4.19-exclusive bit. Direct remap 20 -> 2.
 * DISPLAY         | (Not defined in 4.19)        | -               | ION_DISPLAY_HEAP_ID   | BIT(3)  0x08    | 5.4-only heap.
 * ADSP            | ION_ADSP_HEAP_ID (22)        | BIT(22) 0x400000| ION_ADSP_HEAP_ID      | BIT(4)  0x10    | 4.19-exclusive bit. Direct remap 22 -> 4.
 * AUDIO_ML        | (Not defined in 4.19)        | -               | ION_AUDIO_ML_HEAP_ID  | BIT(5)  0x20    | 5.4-only heap.
 * USER_CONTIG     | ION_USER_CONTIG_HEAP_ID (26) | BIT(26) 0x4000000|ION_USER_CONTIG_HEAP_ID|BIT(6)  0x40   | 4.19-exclusive bit. Direct remap 26 -> 6.
 * QSECOM          | ION_QSECOM_HEAP_ID (27)      | BIT(27) 0x8000000|ION_QSECOM_HEAP_ID    | BIT(7)  0x80    | 4.19-exclusive bit. Direct remap 27 -> 7.
 * AUDIO           | ION_AUDIO_HEAP_ID (28)       | BIT(28)0x10000000|ION_AUDIO_HEAP_ID     | BIT(8)  0x100   | 4.19-exclusive bit. Direct remap 28 -> 8.
 * CP_MM           | ION_CP_MM_HEAP_ID (8)        | BIT(8)  0x100   | ION_CP_MM_HEAP_ID     | BIT(9)  0x200   | Overlaps with 5.4 AUDIO (Bit 8).
 *                 |                              |                 |                       |                 | If flags have ION_FLAG_SECURE or CP flags:
 *                 |                              |                 |                       |                 | remap 8 -> 9 (or 10 if CP_MM absent from DT).
 * SYSTEM_SECURE   | ION_SECURE_HEAP_ID (9)       | BIT(9)  0x200   | ION_SECURE_HEAP_ID    | BIT(10) 0x400   | Overlaps with 5.4 CP_MM (Bit 9).
 *                 |                              |                 |                       |                 | If legacy caller or CP_MM absent from DT:
 *                 |                              |                 |                       |                 | remap 9 -> 10.
 * SECURE_DISPLAY  | ION_SECURE_DISPLAY_HEAP_ID(10)|BIT(10) 0x400   | ION_SECURE_DISPLAY_...| BIT(11) 0x800   | Overlaps with 5.4 SECURE_HEAP (Bit 10).
 *                 |                              |                 |                       |                 | If legacy or flags & ION_FLAG_CP_SEC_DISPLAY:
 *                 |                              |                 |                       |                 | remap 10 -> 11.
 * SPSS            | ION_SPSS_HEAP_ID (13)        | BIT(13) 0x2000  | ION_SPSS_HEAP_ID      | BIT(14) 0x4000  | 4.19-exclusive bit. Direct remap 13 -> 14.
 * SECURE_CARVEOUT | ION_SECURE_CARVEOUT_HEAP_ID(14)|BIT(14)0x4000  | ION_SECURE_CARVEOUT_..| BIT(15) 0x8000  | Overlaps with 5.4 SPSS (Bit 14).
 *                 |                              |                 |                       |                 | If no SPSS flags and (legacy or CP_CDSP/SECURE):
 *                 |                              |                 |                       |                 | remap 14 -> 15.
 * SYSTEM          | ION_SYSTEM_HEAP_ID (25)      | BIT(25)0x2000000| ION_SYSTEM_HEAP_ID    | BIT(25)0x2000000| Identical in 4.19 and 5.4. Preserved as-is.
 * ==============================================================================================================
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/msm_ion.h>
#include "ion_legacy.h"

/* Bits that existed exclusively in 4.19 and are unused in 5.4 */
#define ION_419_EXCLUSIVE_MASK \
	(BIT(13) | BIT(19) | BIT(20) | BIT(22) | BIT(26) | BIT(27) | BIT(28))

/* Bits that have different meanings or conflicting positions between 4.19 and 5.4 */
#define ION_419_OVERLAP_MASK \
	(BIT(8) | BIT(9) | BIT(10) | BIT(14))

unsigned int ion_translate_legacy_heap_mask(struct ion_device *dev,
					    unsigned int old_mask,
					    unsigned int flags)
{
	unsigned int new_mask = old_mask;
	bool is_legacy;

	/*
	 * Fast path: zero overhead for native 5.4 allocations.
	 * If neither 4.19-exclusive nor overlapping bits are present,
	 * return old_mask directly without any remapping logic.
	 */
	if (likely(!(old_mask & (ION_419_EXCLUSIVE_MASK | ION_419_OVERLAP_MASK))))
		return old_mask;

	/*
	 * If any 4.19-exclusive bit is set, the caller is definitively a legacy
	 * blob compiled against 4.19 UAPI headers.
	 */
	is_legacy = !!(old_mask & ION_419_EXCLUSIVE_MASK);

	/* --- 1. Remap 4.19-exclusive bits --- */

	/* 4.19 QSECOM TA: Bit 19 -> 5.4 Bit 1 */
	if (old_mask & BIT(19)) {
		new_mask &= ~BIT(19);
		new_mask |= ION_QSECOM_TA_HEAP_ID; /* BIT(1) */
	}

	/* 4.19 Camera: Bit 20 -> 5.4 Bit 2 */
	if (old_mask & BIT(20)) {
		new_mask &= ~BIT(20);
		new_mask |= ION_CAMERA_HEAP_ID; /* BIT(2) */
	}

	/* 4.19 ADSP: Bit 22 -> 5.4 Bit 4 */
	if (old_mask & BIT(22)) {
		new_mask &= ~BIT(22);
		new_mask |= ION_ADSP_HEAP_ID; /* BIT(4) */
	}

	/* 4.19 User Contig: Bit 26 -> 5.4 Bit 6 */
	if (old_mask & BIT(26)) {
		new_mask &= ~BIT(26);
		new_mask |= ION_USER_CONTIG_HEAP_ID; /* BIT(6) */
	}

	/* 4.19 QSECOM: Bit 27 -> 5.4 Bit 7 */
	if (old_mask & BIT(27)) {
		new_mask &= ~BIT(27);
		new_mask |= ION_QSECOM_HEAP_ID; /* BIT(7) */
	}

	/* 4.19 Audio: Bit 28 -> 5.4 Bit 8 */
	if (old_mask & BIT(28)) {
		new_mask &= ~BIT(28);
		new_mask |= ION_AUDIO_HEAP_ID; /* BIT(8) */
	}

	/* 4.19 SPSS: Bit 13 -> 5.4 Bit 14 */
	if (old_mask & BIT(13)) {
		new_mask &= ~BIT(13);
		new_mask |= ION_SPSS_HEAP_ID; /* BIT(14) */
	}

	/* --- 2. Disambiguate overlapping bits --- */

	/*
	 * Bit 10:
	 *   4.19: ION_SECURE_DISPLAY_HEAP_ID
	 *   5.4 : ION_SECURE_HEAP_ID (Secure Display moved to Bit 11 in 5.4)
	 * Remap to Bit 11 if the caller is confirmed legacy or requested
	 * secure display via ION_FLAG_CP_SEC_DISPLAY.
	 */
	if (old_mask & BIT(10)) {
		if (is_legacy || (flags & ION_FLAG_CP_SEC_DISPLAY)) {
			new_mask &= ~BIT(10);
			new_mask |= ION_SECURE_DISPLAY_HEAP_ID; /* BIT(11) */
		}
	}

	/*
	 * Bit 9:
	 *   4.19: ION_SECURE_HEAP_ID (System Secure Heap)
	 *   5.4 : ION_CP_MM_HEAP_ID (Secure Heap moved to Bit 10 in 5.4)
	 * Remap to Bit 10 if confirmed legacy or if CP_MM is absent on this
	 * platform (SM8150/SDM845 where system secure heap handles CP).
	 */
	if (old_mask & BIT(9)) {
		if (is_legacy || !test_bit(__ffs(ION_CP_MM_HEAP_ID), dev->heap_ids)) {
			new_mask &= ~BIT(9);
			new_mask |= ION_SECURE_HEAP_ID; /* BIT(10) */
		}
	}

	/*
	 * Bit 14:
	 *   4.19: ION_SECURE_CARVEOUT_HEAP_ID
	 *   5.4 : ION_SPSS_HEAP_ID (Secure Carveout moved to Bit 15 in 5.4)
	 * If SPSS flags are explicitly present, preserve 5.4 SPSS (Bit 14).
	 * Otherwise, if confirmed legacy or secure/CDSP flags are set,
	 * remap to 5.4 Secure Carveout (Bit 15).
	 */
	if (old_mask & BIT(14)) {
		if (!(flags & (ION_FLAG_CP_SPSS_SP | ION_FLAG_CP_SPSS_SP_SHARED |
			       ION_FLAG_CP_SPSS_HLOS_SHARED))) {
			if (is_legacy || (flags & (ION_FLAG_CP_CDSP | ION_FLAG_SECURE))) {
				new_mask &= ~BIT(14);
				new_mask |= ION_SECURE_CARVEOUT_HEAP_ID; /* BIT(15) */
			}
		}
	}

	/*
	 * Bit 8:
	 *   4.19: ION_CP_MM_HEAP_ID (Content Protection Multi-Media, secure)
	 *   5.4 : ION_AUDIO_HEAP_ID (Audio streaming, non-secure)
	 * If secure or CP flags are present, caller is asking for 4.19 CP_MM:
	 * remap to 5.4 CP_MM (Bit 9) or fallback to Secure Heap (Bit 10).
	 * If non-secure, caller is asking for Audio heap; leave as Bit 8.
	 */
	if (old_mask & BIT(8)) {
		if (flags & (ION_FLAG_SECURE | ION_FLAGS_CP_MASK)) {
			new_mask &= ~BIT(8);
			if (test_bit(__ffs(ION_CP_MM_HEAP_ID), dev->heap_ids))
				new_mask |= ION_CP_MM_HEAP_ID; /* BIT(9) */
			// else
			// 	new_mask |= ION_SECURE_HEAP_ID; /* BIT(10) */
		}
	}

	if (new_mask != old_mask) {
		pr_err_ratelimited("ion_legacy: %s [%d]: remapped heap mask 0x%08x -> 0x%08x (flags 0x%x)\n",
				     current->comm, task_pid_nr(current),
				     old_mask, new_mask, flags);
	}

	return new_mask;
}
