/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#if defined (__SUNPRO_C) && (__SUNPRO_C >= 0x550)
#define FIRMIR_EXPORT_SYMBOL __global
#elif defined __GNUC__
#define FIRMIR_EXPORT_SYMBOL __attribute__ ((visibility("default")))
#elif defined(_MSC_VER)
#define FIRMIR_EXPORT_SYMBOL __declspec(dllexport)
#else
#define FIRMIR_EXPORT_SYMBOL /* unknown compiler */
#endif
