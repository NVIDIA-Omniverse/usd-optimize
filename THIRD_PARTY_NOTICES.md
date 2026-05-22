# Third-Party Notices

**Scene Optimizer Core** is Copyright (c) 2020-2026, NVIDIA CORPORATION and is licensed
under the Apache License, Version 2.0. See the `LICENSE` file at the root of this
repository for the full product license text, or
https://www.apache.org/licenses/LICENSE-2.0 for the canonical Apache 2.0 license.

This product includes software developed by third parties and/or by NVIDIA. The required
copyright notices, attribution statements, and license texts for those components are
provided below.

The list of components below is generated from the build-time and run-time dependencies
declared in `deps/target-deps.packman.xml` (which transitively imports
`deps/usd-deps.packman.xml`).

---

# 3rd Party Open Source Components

## Pixar Animation Studios - OpenUSD - Tomorrow Open Source Technology License 1.0

Component: `usd-${config}` (version 25.11)

Attribution Statements: The proprietary code links against OpenUSD shared libraries and
uses its C++ and Python APIs pervasively. The core library reads, writes, traverses, and
mutates USD stages. Every operation plugin receives USD prims/stages as input, processes
them, and writes results back through USD APIs. Python extensions also use `pxr` modules
directly. Source code is available at https://github.com/PixarAnimationStudios/OpenUSD.
The full upstream `LICENSE.txt` (which also enumerates the OSS components bundled inside
OpenUSD itself, e.g. RapidJSON, double-conversion, OpenEXR/Half, libdeflate, LZ4, stb,
pugixml, pbrt, Draco, Roboto fonts, Spirv Reflect, khrplatform.h, Tessil robin-map,
CLI11, PEGTL, LibAvif, libaom, boost, etc.) is the authoritative source for those
embedded notices.

License Text(https://github.com/PixarAnimationStudios/OpenUSD/blob/release/LICENSE.txt)

```
Note: The Tomorrow Open Source Technology License 1.0 differs from the
original Apache License 2.0 in the following manner. Section 6 ("Trademarks")
is different.

TOMORROW OPEN SOURCE TECHNOLOGY LICENSE 1.0

   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

   1. Definitions. [...]
   2. Grant of Copyright License. [...]
   3. Grant of Patent License. [...]
   4. Redistribution. [...]
   5. Submission of Contributions. [...]

   6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor
      and its affiliates, except as required to comply with Section 4(c) of
      the License and to reproduce the content of the NOTICE file.

   7. Disclaimer of Warranty. [...]
   8. Limitation of Liability. [...]
   9. Accepting Warranty or Additional Liability. [...]

(See the upstream LICENSE.txt linked above for the full, unabridged text.)
```

---

## Intel Corporation / UXL Foundation - oneTBB (Threading Building Blocks) - Apache License 2.0

Component: `oneTBB` (transitive, bundled with OpenUSD)

Attribution Statements: The proprietary code links against TBB and calls its parallel
algorithms (e.g. `tbb::parallel_for`, `tbb::parallel_reduce`) directly from C++ source to
parallelize mesh processing, spatial analysis, and other compute-heavy operations. TBB is
also a transitive runtime dependency of OpenUSD itself. Source code is available at
https://github.com/uxlfoundation/oneTBB.

License Text(https://github.com/uxlfoundation/oneTBB/blob/master/LICENSE.txt)

```
                                 Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

Copyright (c) 2005-2024 Intel Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

---

## Wenzel Jakob - pybind11 - BSD 3-Clause License

Component: `pybind11` (version 2.11.1)

Attribution Statements: The proprietary code includes pybind11 headers and uses its
macros/types to define Python binding modules that expose the C++ scene optimizer API to
Python. This is a compile-time, header-only interaction. Source code is available at
https://github.com/pybind/pybind11.

License Text(https://github.com/pybind/pybind11/blob/master/LICENSE)

```
Copyright (c) 2016 Wenzel Jakob <wenzel.jakob@epfl.ch>, All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

---

## Eigen - Mozilla Public License 2.0

Component: `Eigen` (transitive, bundled inside the `autouv-core` NVIDIA package)

Attribution Statements: The proprietary code does not include Eigen headers directly.
Eigen is bundled inside the `autouv-core` NVIDIA package and used internally by that
library for linear algebra. Scene Optimizer depends on `autouv-core` at the shared
library level, so Eigen is a transitive, indirect dependency. Source code is available
at https://gitlab.com/libeigen/eigen.

License Text(https://www.mozilla.org/en-US/MPL/2.0/)

```
Eigen is primarily MPL2 licensed. See COPYING.MPL2 and these links:
http://www.mozilla.org/MPL/2.0/
http://www.mozilla.org/MPL/2.0/FAQ.html

Some files contain third-party code under BSD or LGPL licenses, whence the other
COPYING.* files here.

All the LGPL code is either LGPL 2.1-only, or LGPL 2.1-or-later.
For this reason, the COPYING.LGPL file contains the LGPL 2.1 text.

If you want to guarantee that the Eigen code that you are #including is licensed
under the MPL2 and possibly more permissive licenses (like BSD), #define this
preprocessor symbol: EIGEN_MPL2_ONLY
```

---

## Viktor Kirilov - doctest - MIT License

Component: `doctest` (version 2.4.5+nv1-3)

Attribution Statements: The proprietary test code includes doctest headers and uses its
macros (`TEST_CASE`, `CHECK`, etc.) to define and run C++ unit tests. This is a test-only,
compile-time dependency and is not present in shipped binaries. NVIDIA actively chooses
the MIT license to apply to files with this copyright and license. Source code is
available at https://github.com/doctest/doctest.

License Text(https://github.com/doctest/doctest/blob/master/LICENSE.txt)

```
The MIT License (MIT)

Copyright (c) 2016-2023 Viktor Kirilov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

# Attribution for First-Party NVIDIA Components

The following components are authored and distributed by NVIDIA CORPORATION and are
covered by the NVIDIA proprietary license shown below (or, where applicable, the
component-specific NVIDIA Software License Agreement / EULA noted alongside the entry).
These components may themselves bundle additional third-party software whose notices are
covered by their own respective Third-Party Notice files distributed with each package.

## NVIDIA CORPORATION - Carbonite SDK & Plugins - NVIDIA Proprietary

Component: `carb_sdk_plugins` (version 210.1.4+release.12772.a4efbbdc.gl)

Attribution Statements: Header-only usage for logging and profiling macros. Distributed
under the NVIDIA proprietary license. Refer to the package's bundled `LICENSE` and
`THIRD_PARTY_NOTICES` files for any embedded third-party components.

License Text (NVIDIA Proprietary)

```
Copyright (c) 2020-2026, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property and
proprietary rights in and to this software, related documentation and any
modifications thereto. Any use, reproduction, disclosure or distribution of
this software and related documentation without an express license agreement
from NVIDIA CORPORATION is strictly prohibited.
```

---

## NVIDIA CORPORATION - omnimesh_ops_usd - NVIDIA Proprietary

Component: `omnimesh_ops_usd` (version 110.0.2)

Attribution Statements: NVIDIA-developed mesh-operations library used by Scene Optimizer
operations. Refer to the package's bundled `LICENSE` and `THIRD_PARTY_NOTICES` files for
any embedded third-party components.

License Text (NVIDIA Proprietary)

```
Copyright (c) 2020-2026, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property and
proprietary rights in and to this software, related documentation and any
modifications thereto. Any use, reproduction, disclosure or distribution of
this software and related documentation without an express license agreement
from NVIDIA CORPORATION is strictly prohibited.
```

---

## NVIDIA CORPORATION - mesh_tools - NVIDIA Proprietary

Component: `mesh_tools` (version 110.0.15)

Attribution Statements: NVIDIA-developed mesh-processing toolkit. Refer to the package's
bundled `LICENSE` and `THIRD_PARTY_NOTICES` files for any embedded third-party components.

License Text (NVIDIA Proprietary)

```
Copyright (c) 2020-2026, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property and
proprietary rights in and to this software, related documentation and any
modifications thereto. Any use, reproduction, disclosure or distribution of
this software and related documentation without an express license agreement
from NVIDIA CORPORATION is strictly prohibited.
```

---

## NVIDIA CORPORATION - autouv-core - NVIDIA Proprietary

Component: `autouv-core` (version 1.0.2)

Attribution Statements: NVIDIA-developed automatic UV unwrapping library. Bundles Eigen
(see the Eigen entry above for its OSS notice). Refer to the package's bundled `LICENSE`
and `THIRD_PARTY_NOTICES` files for any additional embedded third-party components.

License Text (NVIDIA Proprietary)

```
Copyright (c) 2020-2026, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property and
proprietary rights in and to this software, related documentation and any
modifications thereto. Any use, reproduction, disclosure or distribution of
this software and related documentation without an express license agreement
from NVIDIA CORPORATION is strictly prohibited.
```

---

## NVIDIA CORPORATION - CUDA Toolkit - NVIDIA Software License Agreement

Component: `cuda` (version 12.4.1, Linux only)

Attribution Statements: NVIDIA CUDA Toolkit. The CUDA Toolkit is governed by the NVIDIA
Software License Agreement and the supplemental terms for CUDA. The full agreement and
the per-component Third-Party Notices for CUDA are distributed with the toolkit.

License Text(https://docs.nvidia.com/cuda/eula/index.html)

```
Copyright (c) 2007-2026 NVIDIA Corporation. All rights reserved.

This software and the related documents are NVIDIA confidential and proprietary
information, and are licensed under the terms of the NVIDIA Software License
Agreement available at https://docs.nvidia.com/cuda/eula/index.html, including
the supplement for the CUDA Toolkit. The CUDA Toolkit also bundles additional
third-party software whose copyright and license notices are reproduced in the
"EULA.txt" / "Third Party Notices" files distributed inside the toolkit.
```

---

## NVIDIA CORPORATION - shrinkwrap_openvdb - NVIDIA Proprietary

Component: `shrinkwrap_openvdb` (version 13.0.1)

Attribution Statements: NVIDIA-developed shrink-wrap library that incorporates the
OpenVDB volumetric library (Apache 2.0). The underlying OpenVDB notices are reproduced
in the package's bundled `THIRD_PARTY_NOTICES` file.

License Text (NVIDIA Proprietary)

```
Copyright (c) 2020-2026, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property and
proprietary rights in and to this software, related documentation and any
modifications thereto. Any use, reproduction, disclosure or distribution of
this software and related documentation without an express license agreement
from NVIDIA CORPORATION is strictly prohibited.
```

---

# Notes

* This file enumerates the packages declared in `deps/target-deps.packman.xml` (and the
  USD/Python imports it pulls from `deps/usd-deps.packman.xml`). Build-host-only and
  developer tooling dependencies (`deps/host-deps.packman.xml`, `deps/repo-deps.packman.xml`,
  and the optional `deps/repo-deps-nv.packman.xml` side-car) are not included here because
  they are not redistributed with the product.
* **Python (CPython, PSF License)** is pulled in via `deps/usd-deps.packman.xml` as a
  host-environment dependency used only for building the pybind11 bindings. The CPython
  interpreter is not redistributed as part of Scene Optimizer Core (consumers supply
  their own Python runtime), so it is intentionally omitted from the OSS attribution
  list above. If a downstream product bundles the CPython runtime sourced from this
  package, that product must add the PSF License attribution to its own notices file.
* For each NVIDIA-distributed package above, the canonical and most up-to-date copyright,
  license, and embedded third-party notices are the ones shipped inside that package
  (typically as `LICENSE`, `LICENSE.txt`, or `THIRD_PARTY_NOTICES`). The entries here are a
  product-level summary intended to satisfy the OSS attribution requirement at the level
  of the Scene Optimizer Core distribution.
