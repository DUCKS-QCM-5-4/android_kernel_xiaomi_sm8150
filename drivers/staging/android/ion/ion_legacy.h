/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) TODO
 *
 * Header for ION legacy 4.19/4.9 userspace blob compatibility layer.
 */

#ifndef _ION_LEGACY_H
#define _ION_LEGACY_H

#include "ion_private.h"

#if IS_ENABLED(CONFIG_ION_LEGACY_SUPPORT)

unsigned int ion_translate_legacy_heap_mask(struct ion_device *dev,
					    unsigned int old_mask,
					    unsigned int flags);

#else /* !CONFIG_ION_LEGACY_SUPPORT */

static inline unsigned int ion_translate_legacy_heap_mask(struct ion_device *dev,
							  unsigned int old_mask,
							  unsigned int flags)
{
	return old_mask;
}

#endif /* CONFIG_ION_LEGACY_SUPPORT */

#endif /* _ION_LEGACY_H */
