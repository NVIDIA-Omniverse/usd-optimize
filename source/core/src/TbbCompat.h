// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// USD 25.05 ships classic TBB, while USD 25.11 ships oneTBB. Gate this on
// the USD version instead of probing headers so system oneTBB headers cannot
// be mixed with a classic TBB USD bundle.
#include <pxr/pxr.h>

#if PXR_VERSION >= 2511
#    include <tbb/parallel_for_each.h>
#    include <tbb/parallel_pipeline.h>
#else
#    include <tbb/parallel_do.h>
#    include <tbb/pipeline.h>
#endif

namespace omni::scene::optimizer::tbbcompat
{

#if PXR_VERSION >= 2511

inline constexpr tbb::filter_mode parallelFilterMode = tbb::filter_mode::parallel;
inline constexpr tbb::filter_mode serialOutOfOrderFilterMode = tbb::filter_mode::serial_out_of_order;

template <typename T>
using Feeder = tbb::feeder<T>;

template <typename Iterator, typename Function>
void parallelForEach(Iterator first, Iterator last, const Function& function)
{
    tbb::parallel_for_each(first, last, function);
}

#else

inline constexpr tbb::filter::mode parallelFilterMode = tbb::filter::parallel;
inline constexpr tbb::filter::mode serialOutOfOrderFilterMode = tbb::filter::serial_out_of_order;

template <typename T>
using Feeder = tbb::parallel_do_feeder<T>;

template <typename Iterator, typename Function>
void parallelForEach(Iterator first, Iterator last, const Function& function)
{
    tbb::parallel_do(first, last, function);
}

#endif

} // namespace omni::scene::optimizer::tbbcompat
