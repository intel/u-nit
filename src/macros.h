/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef MACROS_HEADER_
#define MACROS_HEADER_

#ifdef __GNUC__
#define FORMAT_PRINTF(fmt, arg) __attribute__((format(printf, fmt, arg)))
#else
#define FORMAT_PRINTF(fmt, arg)
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#endif /*__MACROS_H__*/
