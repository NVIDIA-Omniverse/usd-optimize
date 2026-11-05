# Third-Party Notices

**Usd Optimize Core** is Copyright (c) 2020-2026, NVIDIA CORPORATION and is licensed
under the Apache License, Version 2.0. See the `LICENSE` file at the root of this
repository for the full product license text, or
https://www.apache.org/licenses/LICENSE-2.0 for the canonical Apache 2.0 license.

This product includes software developed by third parties and/or by NVIDIA. The required
copyright notices, attribution statements, and license texts for those components are
provided below.

The list of components below reflects the build-time and run-time dependencies
declared in `deps/target-deps.packman.xml` (which transitively imports
`deps/usd-deps.generated.packman.xml` and, for USD-version-specific libraries,
`deps/usd-lib-deps.generated.packman.xml` sourced from `deps/usd-lib-deps.json`).
Version strings match the packman package pins for the default build — selected by the
`usd_ver` token in `repo.toml`, currently 26.08 — unless noted otherwise. The list also
covers the third-party code that the drops redistribute inside those packages.

---

# 3rd Party Open Source Components

## Pixar Animation Studios - OpenUSD - Tomorrow Open Source Technology License 1.0

Component: `usd-${config}` (version 26.08; packman package `openusd`
`0.26.08-9-<platform>-dynamic-${config}-py312.no_imaging`)

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

Component: `oneTBB` (packman package `onetbb` `2021.13.0-4-<platform>-${config}` on USD
26.08; through 25.11 it is instead bundled inside the OpenUSD package)

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

## Inria, Université Bordeaux, Cisco Systems and other contributors - hwloc (Portable Hardware Locality) - BSD 3-Clause License

Component: hwloc (version 2.9.3, built into oneTBB's `tbbbind_2_5.dll` in the Windows USD
25.11 drop)

Attribution Statements: Through 25.11 oneTBB ships inside the OpenUSD package, and that build
has hwloc built into `tbbbind_2_5.dll`, which the Windows 25.11 drop ships in `extraLibs/`.
TBB loads it only to bind work to NUMA nodes or core types, which Usd Optimize does not do.
In the other drops `tbbbind_2_5` links hwloc dynamically, and no drop ships hwloc itself.
Source code is available at https://github.com/open-mpi/hwloc.

License Text(https://github.com/open-mpi/hwloc/blob/hwloc-2.9.3/COPYING)

```
Copyright © 2004-2006 The Trustees of Indiana University and Indiana University Research and Technology Corporation.  All rights reserved.
Copyright © 2004-2005 The University of Tennessee and The University of Tennessee Research Foundation.  All rights reserved.
Copyright © 2004-2005 High Performance Computing Center Stuttgart, University of Stuttgart.  All rights reserved.
Copyright © 2004-2005 The Regents of the University of California. All rights reserved.
Copyright © 2009      CNRS
Copyright © 2009-2016 Inria.  All rights reserved.
Copyright © 2009-2015 Université Bordeaux
Copyright © 2009-2015 Cisco Systems, Inc.  All rights reserved.
Copyright © 2009-2012 Oracle and/or its affiliates.  All rights reserved.
Copyright © 2010      IBM
Copyright © 2010      Jirka Hladky
Copyright © 2012      Aleksej Saushev, The NetBSD Foundation
Copyright © 2012      Blue Brain Project, EPFL. All rights reserved.
Copyright © 2013-2014 University of Wisconsin-La Crosse. All rights reserved.
Copyright © 2015      Research Organization for Information Science and Technology (RIST). All rights reserved.
Copyright © 2015-2016 Intel, Inc.  All rights reserved.
See COPYING in top-level directory.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## Academy Software Foundation - MaterialX - Apache License 2.0

Component: `MaterialX` (packman package `materialx` `1.39.5-14-<platform>-${config}-py312`
on USD 26.08; through 25.11 it is instead bundled inside the OpenUSD package)

Attribution Statements: MaterialX is redistributed in `extraLibs/` as the runtime that
OpenUSD's `usdMtlx` plugin depends on — `usd_usdMtlx` ships from the OpenUSD package's
`lib/`, while its `MaterialX*` runtime libraries come from the MaterialX package (see
`source/core/premake5.lua`). Usd Optimize's own source does not call MaterialX APIs; the
libraries are present so that stages referencing MaterialX shading networks resolve at
runtime. Source code is available at
https://github.com/AcademySoftwareFoundation/MaterialX.

License Text(https://github.com/AcademySoftwareFoundation/MaterialX/blob/main/LICENSE)

```
                                 Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

Copyright Contributors to the MaterialX Project

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

## Lucasfilm Ltd. and Sony Pictures Imageworks - Alembic - BSD 3-Clause License

Component: Alembic (version 1.8.5, bundled inside the OpenUSD 25.11 package)

Attribution Statements: The `usd/<config>/lib/*` copy rule in `source/core/premake5.lua`
takes the OpenUSD 25.11 package's `lib/` directory whole, so the 25.11 drops ship Alembic in
`extraLibs/`: `libAlembic.so.1.8.5` on Linux, `Alembic.dll` and its import library on
Windows. Usd Optimize does not call Alembic, and the drops do not include OpenUSD's `usdAbc`
plugin, which is what loads it. The 26.08 drops do not contain Alembic. Alembic's license
ends with its required notices for distribution (Attachment A), reproduced with it below.
Source code is available at https://github.com/alembic/alembic.

License Text(https://github.com/alembic/alembic/blob/master/LICENSE.txt)

```
TM & © 2009-2015 Lucasfilm Entertainment Company Ltd. or Lucasfilm Ltd.
All rights reserved.

Industrial Light & Magic, ILM and the Bulb and Gear design logo are all
registered trademarks or service marks of Lucasfilm Ltd.

© 2009-2015 Sony Pictures Imageworks Inc.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
* Neither the name of Industrial Light & Magic nor the names of
its contributors may be used to endorse or promote products derived
from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


-------------------------------------------------------------------------------

ALEMBIC ATTACHMENT A —
REQUIRED NOTICES FOR DISTRIBUTION

The Alembic Software is distributed along with certain third party
components licensed under various open source software licenses ("Open
Source Components"). In addition to the warranty disclaimers contained
in the open source licenses found below, Industrial Light & Magic, a
division of Lucasfilm Entertainment Company Ltd. ("ILM") makes the
following disclaimers regarding the Open Source Components on behalf of
itself, the copyright holders, contributors, and licensors of such Open
Source Components:

TO THE FULLEST EXTENT PERMITTED UNDER APPLICABLE LAW, THE OPEN SOURCE
COMPONENTS ARE PROVIDED BY THE COPYRIGHT HOLDERS, CONTRIBUTORS,
LICENSORS, AND ILM "AS IS" AND ANY REPRESENTATIONS OR WARRANTIES OF ANY
KIND, WHETHER ORAL OR WRITTEN, WHETHER EXPRESS, IMPLIED, OR ARISING BY
STATUTE, CUSTOM, COURSE OF DEALING, OR TRADE USAGE, INCLUDING WITHOUT
LIMITATION THE IMPLIED WARRANTIES OF TITLE, MERCHANTABILITY, FITNESS FOR
A PARTICULAR PURPOSE, AND NON-INFRINGEMENT, ARE DISCLAIMED. IN NO EVENT
WILL THE COPYRIGHT OWNER, CONTRIBUTORS, LICENSORS, OR ILM AND/OR ITS
AFFILIATES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION), HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THE OPEN
SOURCE COMPONENTS, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Boost C++ Libraries
------------------------------------------------------------------------

Boost Software License – Version 1.0 August 17th, 2003 Permission is
hereby granted, free of charge, to any person or organization obtaining
a copy of the software and accompanying documentation covered by this
license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of
the Software, and to permit third-parties to whom the Software is
furnished to do so, all subject to the following:

The copyright notices in the Software and this entire statement,
including the above license grant, this restriction and the following
disclaimer, must be included in all copies of the Software, in whole or
in part, and all derivative works of the Software, unless such copies or
derivative works are solely in the form of machine-executable object
code generated by a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR ANYONE
DISTRIBUTING THE SOFTWARE BE LIABLE FOR ANY DAMAGES OR OTHER LIABILITY,
WHETHER IN CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Academy Software Foundation - Imath - BSD 3-Clause License

Component: Imath (version 3.1, bundled inside the OpenUSD 25.11 package)

Attribution Statements: The same copy rule ships Imath in the 25.11 drops only:
`libImath-3_1.so.29.11.0` on Linux, and on Windows only its import library `Imath-3_1.lib`.
Usd Optimize does not call Imath. Source code is available at
https://github.com/AcademySoftwareFoundation/Imath.

License Text(https://github.com/AcademySoftwareFoundation/Imath/blob/main/LICENSE.md)

```
Copyright Contributors to the OpenEXR Project. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## Pixar Animation Studios - OpenSubdiv - Modified Apache License 2.0

Component: OpenSubdiv (version 3.6.0, bundled inside the OpenUSD 25.11 package)

Attribution Statements: The same copy rule ships OpenSubdiv in the 25.11 drops only:
`libosdCPU.so.3.6.0` and `libosdGPU.so.3.6.0` on Linux; on Windows the static libraries
`osdCPU.lib` and `osdGPU.lib`, and OpenUSD's `usd_pxOsd.dll` has OpenSubdiv built in.
Usd Optimize does not call OpenSubdiv. Its license is the Apache License 2.0 (full text in
`LICENSE`) with Section 6 replaced as quoted below. Source code is available at
https://github.com/PixarAnimationStudios/OpenSubdiv.

License Text(https://github.com/PixarAnimationStudios/OpenSubdiv/blob/v3_6_0/LICENSE.txt)

```
                               Modified Apache 2.0 License

   [Sections 1-5 and 7-9 match the Apache License, Version 2.0.]

   6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor
      and its affiliates, except as required to comply with Section 4(c) of
      the License and to reproduce the content of the NOTICE file.
```

NOTICE(https://github.com/PixarAnimationStudios/OpenSubdiv/blob/v3_6_0/NOTICE.txt)

```
OpenSubdiv
Copyright 2013 Pixar
All rights reserved.

This product includes software developed at:
    Pixar (http://www.pixar.com/).
    Dreamworks Animation (http://www.dreamworksanimation.com/)
    Autodesk, Inc. (http://www.autodesk.com/).
    Google, Inc. (http://www.google.com/).
    DigitalFish (http://digitalfish.com/).
```

---

## Jean-loup Gailly and Mark Adler - zlib - zlib License

Component: zlib (version 1.3.1 in the OpenUSD 25.11 package, 1.3.2 in the CPython runtime
library)

Attribution Statements: zlib ships in two forms. The same copy rule ships it in the 25.11
drops: `libz.so.1.3.1` on Linux, and on Windows only its import library `zdll.lib`. Every
Linux drop also has it built into the CPython runtime library `libpython3.12.so.1.0` (see
the CPython entry below). Usd Optimize does not call zlib, which performs compression only.
Source code is available at https://github.com/madler/zlib.

License Text(https://github.com/madler/zlib/blob/v1.3.1/LICENSE)

```
 zlib.h -- interface of the 'zlib' general purpose compression library
  version 1.3.1, January 22nd, 2024

  Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu


  The data format used by the zlib library is described by RFCs (Request for
  Comments) 1950 to 1952 in the files http://tools.ietf.org/html/rfc1950
  (zlib format), rfc1951 (deflate format) and rfc1952 (gzip format).
```

---

## Python Software Foundation - CPython runtime library - Python Software Foundation License Version 2

Component: `python` (packman package `python` `3.12.16+nv1-<platform>`), the runtime library
only

Attribution Statements: Usd Optimize's core library and the bundled OpenUSD libraries link
against the CPython runtime library, so every drop ships it in `extraLibs/`: `python312.dll`
on Windows (copied by `source/core/premake5.lua`), `libpython3.12.so.1.0` and `libpython3.so`
on Linux (copied by `source/cli/premake5.lua`). The drops do not ship the Python interpreter,
the standard library or CPython's own extension modules; consumers supply their own Python
3.12. CPython incorporates third-party code; its "Licenses and Acknowledgements for
Incorporated Software" (https://docs.python.org/3.12/license.html) is the authoritative
source for those notices. In addition, `python312.dll` contains HACL*, and the Linux
`libpython3.12.so.1.0` has zlib 1.3.2, bzip2 1.0.8 and liblzma 5.8.3 built in; each has its
own entry. Source code is available at https://github.com/python/cpython.

License Text(https://github.com/python/cpython/blob/v3.12.16/LICENSE)

```
A. HISTORY OF THE SOFTWARE
==========================

Python was created in the early 1990s by Guido van Rossum at Stichting
Mathematisch Centrum (CWI, see https://www.cwi.nl) in the Netherlands
as a successor of a language called ABC.  Guido remains Python's
principal author, although it includes many contributions from others.

In 1995, Guido continued his work on Python at the Corporation for
National Research Initiatives (CNRI, see https://www.cnri.reston.va.us)
in Reston, Virginia where he released several versions of the
software.

In May 2000, Guido and the Python core development team moved to
BeOpen.com to form the BeOpen PythonLabs team.  In October of the same
year, the PythonLabs team moved to Digital Creations, which became
Zope Corporation.  In 2001, the Python Software Foundation (PSF, see
https://www.python.org/psf/) was formed, a non-profit organization
created specifically to own Python-related Intellectual Property.
Zope Corporation was a sponsoring member of the PSF.

All Python releases are Open Source (see https://opensource.org for
the Open Source Definition).  Historically, most, but not all, Python
releases have also been GPL-compatible; the table below summarizes
the various releases.

    Release         Derived     Year        Owner       GPL-
                    from                                compatible? (1)

    0.9.0 thru 1.2              1991-1995   CWI         yes
    1.3 thru 1.5.2  1.2         1995-1999   CNRI        yes
    1.6             1.5.2       2000        CNRI        no
    2.0             1.6         2000        BeOpen.com  no
    1.6.1           1.6         2001        CNRI        yes (2)
    2.1             2.0+1.6.1   2001        PSF         no
    2.0.1           2.0+1.6.1   2001        PSF         yes
    2.1.1           2.1+2.0.1   2001        PSF         yes
    2.1.2           2.1.1       2002        PSF         yes
    2.1.3           2.1.2       2002        PSF         yes
    2.2 and above   2.1.1       2001-now    PSF         yes

Footnotes:

(1) GPL-compatible doesn't mean that we're distributing Python under
    the GPL.  All Python licenses, unlike the GPL, let you distribute
    a modified version without making your changes open source.  The
    GPL-compatible licenses make it possible to combine Python with
    other software that is released under the GPL; the others don't.

(2) According to Richard Stallman, 1.6.1 is not GPL-compatible,
    because its license has a choice of law clause.  According to
    CNRI, however, Stallman's lawyer has told CNRI's lawyer that 1.6.1
    is "not incompatible" with the GPL.

Thanks to the many outside volunteers who have worked under Guido's
direction to make these releases possible.


B. TERMS AND CONDITIONS FOR ACCESSING OR OTHERWISE USING PYTHON
===============================================================

Python software and documentation are licensed under the
Python Software Foundation License Version 2.

Starting with Python 3.8.6, examples, recipes, and other code in
the documentation are dual licensed under the PSF License Version 2
and the Zero-Clause BSD license.

Some software incorporated into Python is under different licenses.
The licenses are listed with code falling under that license.


PYTHON SOFTWARE FOUNDATION LICENSE VERSION 2
--------------------------------------------

1. This LICENSE AGREEMENT is between the Python Software Foundation
("PSF"), and the Individual or Organization ("Licensee") accessing and
otherwise using this software ("Python") in source or binary form and
its associated documentation.

2. Subject to the terms and conditions of this License Agreement, PSF hereby
grants Licensee a nonexclusive, royalty-free, world-wide license to reproduce,
analyze, test, perform and/or display publicly, prepare derivative works,
distribute, and otherwise use Python alone or in any derivative version,
provided, however, that PSF's License Agreement and PSF's notice of copyright,
i.e., "Copyright (c) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010,
2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020, 2021, 2022, 2023 Python Software Foundation;
All Rights Reserved" are retained in Python alone or in any derivative version
prepared by Licensee.

3. In the event Licensee prepares a derivative work that is based on
or incorporates Python or any part thereof, and wants to make
the derivative work available to others as provided herein, then
Licensee hereby agrees to include in any such work a brief summary of
the changes made to Python.

4. PSF is making Python available to Licensee on an "AS IS"
basis.  PSF MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR
IMPLIED.  BY WAY OF EXAMPLE, BUT NOT LIMITATION, PSF MAKES NO AND
DISCLAIMS ANY REPRESENTATION OR WARRANTY OF MERCHANTABILITY OR FITNESS
FOR ANY PARTICULAR PURPOSE OR THAT THE USE OF PYTHON WILL NOT
INFRINGE ANY THIRD PARTY RIGHTS.

5. PSF SHALL NOT BE LIABLE TO LICENSEE OR ANY OTHER USERS OF PYTHON
FOR ANY INCIDENTAL, SPECIAL, OR CONSEQUENTIAL DAMAGES OR LOSS AS
A RESULT OF MODIFYING, DISTRIBUTING, OR OTHERWISE USING PYTHON,
OR ANY DERIVATIVE THEREOF, EVEN IF ADVISED OF THE POSSIBILITY THEREOF.

6. This License Agreement will automatically terminate upon a material
breach of its terms and conditions.

7. Nothing in this License Agreement shall be deemed to create any
relationship of agency, partnership, or joint venture between PSF and
Licensee.  This License Agreement does not grant permission to use PSF
trademarks or trade name in a trademark sense to endorse or promote
products or services of Licensee, or any third party.

8. By copying, installing or otherwise using Python, Licensee
agrees to be bound by the terms and conditions of this License
Agreement.


BEOPEN.COM LICENSE AGREEMENT FOR PYTHON 2.0
-------------------------------------------

BEOPEN PYTHON OPEN SOURCE LICENSE AGREEMENT VERSION 1

1. This LICENSE AGREEMENT is between BeOpen.com ("BeOpen"), having an
office at 160 Saratoga Avenue, Santa Clara, CA 95051, and the
Individual or Organization ("Licensee") accessing and otherwise using
this software in source or binary form and its associated
documentation ("the Software").

2. Subject to the terms and conditions of this BeOpen Python License
Agreement, BeOpen hereby grants Licensee a non-exclusive,
royalty-free, world-wide license to reproduce, analyze, test, perform
and/or display publicly, prepare derivative works, distribute, and
otherwise use the Software alone or in any derivative version,
provided, however, that the BeOpen Python License is retained in the
Software, alone or in any derivative version prepared by Licensee.

3. BeOpen is making the Software available to Licensee on an "AS IS"
basis.  BEOPEN MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR
IMPLIED.  BY WAY OF EXAMPLE, BUT NOT LIMITATION, BEOPEN MAKES NO AND
DISCLAIMS ANY REPRESENTATION OR WARRANTY OF MERCHANTABILITY OR FITNESS
FOR ANY PARTICULAR PURPOSE OR THAT THE USE OF THE SOFTWARE WILL NOT
INFRINGE ANY THIRD PARTY RIGHTS.

4. BEOPEN SHALL NOT BE LIABLE TO LICENSEE OR ANY OTHER USERS OF THE
SOFTWARE FOR ANY INCIDENTAL, SPECIAL, OR CONSEQUENTIAL DAMAGES OR LOSS
AS A RESULT OF USING, MODIFYING OR DISTRIBUTING THE SOFTWARE, OR ANY
DERIVATIVE THEREOF, EVEN IF ADVISED OF THE POSSIBILITY THEREOF.

5. This License Agreement will automatically terminate upon a material
breach of its terms and conditions.

6. This License Agreement shall be governed by and interpreted in all
respects by the law of the State of California, excluding conflict of
law provisions.  Nothing in this License Agreement shall be deemed to
create any relationship of agency, partnership, or joint venture
between BeOpen and Licensee.  This License Agreement does not grant
permission to use BeOpen trademarks or trade names in a trademark
sense to endorse or promote products or services of Licensee, or any
third party.  As an exception, the "BeOpen Python" logos available at
http://www.pythonlabs.com/logos.html may be used according to the
permissions granted on that web page.

7. By copying, installing or otherwise using the software, Licensee
agrees to be bound by the terms and conditions of this License
Agreement.


CNRI LICENSE AGREEMENT FOR PYTHON 1.6.1
---------------------------------------

1. This LICENSE AGREEMENT is between the Corporation for National
Research Initiatives, having an office at 1895 Preston White Drive,
Reston, VA 20191 ("CNRI"), and the Individual or Organization
("Licensee") accessing and otherwise using Python 1.6.1 software in
source or binary form and its associated documentation.

2. Subject to the terms and conditions of this License Agreement, CNRI
hereby grants Licensee a nonexclusive, royalty-free, world-wide
license to reproduce, analyze, test, perform and/or display publicly,
prepare derivative works, distribute, and otherwise use Python 1.6.1
alone or in any derivative version, provided, however, that CNRI's
License Agreement and CNRI's notice of copyright, i.e., "Copyright (c)
1995-2001 Corporation for National Research Initiatives; All Rights
Reserved" are retained in Python 1.6.1 alone or in any derivative
version prepared by Licensee.  Alternately, in lieu of CNRI's License
Agreement, Licensee may substitute the following text (omitting the
quotes): "Python 1.6.1 is made available subject to the terms and
conditions in CNRI's License Agreement.  This Agreement together with
Python 1.6.1 may be located on the internet using the following
unique, persistent identifier (known as a handle): 1895.22/1013.  This
Agreement may also be obtained from a proxy server on the internet
using the following URL: http://hdl.handle.net/1895.22/1013".

3. In the event Licensee prepares a derivative work that is based on
or incorporates Python 1.6.1 or any part thereof, and wants to make
the derivative work available to others as provided herein, then
Licensee hereby agrees to include in any such work a brief summary of
the changes made to Python 1.6.1.

4. CNRI is making Python 1.6.1 available to Licensee on an "AS IS"
basis.  CNRI MAKES NO REPRESENTATIONS OR WARRANTIES, EXPRESS OR
IMPLIED.  BY WAY OF EXAMPLE, BUT NOT LIMITATION, CNRI MAKES NO AND
DISCLAIMS ANY REPRESENTATION OR WARRANTY OF MERCHANTABILITY OR FITNESS
FOR ANY PARTICULAR PURPOSE OR THAT THE USE OF PYTHON 1.6.1 WILL NOT
INFRINGE ANY THIRD PARTY RIGHTS.

5. CNRI SHALL NOT BE LIABLE TO LICENSEE OR ANY OTHER USERS OF PYTHON
1.6.1 FOR ANY INCIDENTAL, SPECIAL, OR CONSEQUENTIAL DAMAGES OR LOSS AS
A RESULT OF MODIFYING, DISTRIBUTING, OR OTHERWISE USING PYTHON 1.6.1,
OR ANY DERIVATIVE THEREOF, EVEN IF ADVISED OF THE POSSIBILITY THEREOF.

6. This License Agreement will automatically terminate upon a material
breach of its terms and conditions.

7. This License Agreement shall be governed by the federal
intellectual property law of the United States, including without
limitation the federal copyright law, and, to the extent such
U.S. federal law does not apply, by the law of the Commonwealth of
Virginia, excluding Virginia's conflict of law provisions.
Notwithstanding the foregoing, with regard to derivative works based
on Python 1.6.1 that incorporate non-separable material that was
previously distributed under the GNU General Public License (GPL), the
law of the Commonwealth of Virginia shall govern this License
Agreement only as to issues arising under or with respect to
Paragraphs 4, 5, and 7 of this License Agreement.  Nothing in this
License Agreement shall be deemed to create any relationship of
agency, partnership, or joint venture between CNRI and Licensee.  This
License Agreement does not grant permission to use CNRI trademarks or
trade name in a trademark sense to endorse or promote products or
services of Licensee, or any third party.

8. By clicking on the "ACCEPT" button where indicated, or by copying,
installing or otherwise using Python 1.6.1, Licensee agrees to be
bound by the terms and conditions of this License Agreement.

        ACCEPT


CWI LICENSE AGREEMENT FOR PYTHON 0.9.0 THROUGH 1.2
--------------------------------------------------

Copyright (c) 1991 - 1995, Stichting Mathematisch Centrum Amsterdam,
The Netherlands.  All rights reserved.

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Stichting Mathematisch
Centrum or CWI not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

STICHTING MATHEMATISCH CENTRUM DISCLAIMS ALL WARRANTIES WITH REGARD TO
THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS, IN NO EVENT SHALL STICHTING MATHEMATISCH CENTRUM BE LIABLE
FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

ZERO-CLAUSE BSD LICENSE FOR CODE IN THE PYTHON DOCUMENTATION
----------------------------------------------------------------------

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
```

---

## INRIA, CMU, Microsoft Corporation and HACL* Contributors - HACL* - MIT License

Component: HACL* (CPython 3.12.16's `Modules/_hacl`, built into `python312.dll`)

Attribution Statements: CPython's Windows build compiles HACL*'s MD5, SHA-1, SHA-2 and SHA-3
code into `python312.dll`, which the Windows drops ship in `extraLibs/`. Usd Optimize does not
call these hash functions. The Linux `libpython3.12.so.1.0` does not contain them. Source code
is available at https://github.com/hacl-star/hacl-star.

License Text(https://github.com/python/cpython/blob/v3.12.16/Modules/_hacl/Hacl_Hash_SHA3.c)

```
MIT License

Copyright (c) 2016-2022 INRIA, CMU and Microsoft Corporation
Copyright (c) 2022-2023 HACL* Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Julian Seward - bzip2 (libbzip2) - bzip2 License

Component: bzip2 (version 1.0.8, built into the Linux CPython runtime library)

Attribution Statements: The packman CPython build compiles libbzip2 into
`libpython3.12.so.1.0` for CPython's `bz2` module, so every Linux drop ships it inside that
library. Usd Optimize does not use bzip2 compression. The Windows drops do not contain it.
Source code is available at https://sourceware.org/bzip2/.

License Text(packman package `python`, `PACKAGE-LICENSES/bzip2-LICENSE.txt`)

```
This program, "bzip2", the associated library "libbzip2", and all
documentation, are copyright (C) 1996-2019 Julian R Seward.  All
rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. The origin of this software must not be misrepresented; you must
   not claim that you wrote the original software.  If you use this
   software in a product, an acknowledgment in the product
   documentation would be appreciated but is not required.

3. Altered source versions must be plainly marked as such, and must
   not be misrepresented as being the original software.

4. The name of the author may not be used to endorse or promote
   products derived from this software without specific prior written
   permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Julian Seward, jseward@acm.org
bzip2/libbzip2 version 1.0.8 of 13 July 2019
```

---

## The XZ Utils authors and contributors - liblzma (XZ Utils) - BSD Zero Clause License

Component: liblzma (XZ Utils version 5.8.3, built into the Linux CPython runtime library)

Attribution Statements: The packman CPython build compiles liblzma into
`libpython3.12.so.1.0` for CPython's `lzma` module, so every Linux drop ships it inside that
library. Usd Optimize does not use LZMA compression. The Windows drops do not contain it.
Source code is available at https://github.com/tukaani-project/xz.

License Text(https://github.com/tukaani-project/xz/blob/v5.8.3/COPYING.0BSD)

```
Copyright (C) The XZ Utils authors and contributors

Permission to use, copy, modify, and/or distribute this
software for any purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

---

## Wenzel Jakob - pybind11 - BSD 3-Clause License

Component: `pybind11` (version 2.11.1-0)

Attribution Statements: The proprietary code includes pybind11 headers and uses its
macros/types to define Python binding modules that expose the C++ usd optimize API to
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
library for linear algebra. Usd Optimize depends on `autouv-core` at the shared
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

Component: `omnimesh_ops_usd` (packman package pinned per USD version in
`deps/usd-lib-deps.json`)

Attribution Statements: NVIDIA-developed mesh-operations library used by Usd Optimize
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

## NVIDIA CORPORATION - mesh_tools_lib - NVIDIA Proprietary

Component: `mesh_tools_lib`. Linked as `mesh_tools` in the build tree
(`deps/target-deps.packman.xml`).

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

Component: `autouv-core` (USD-version-specific packman pin in `deps/usd-lib-deps.json`)

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

Component: `shrinkwrap_openvdb` (USD-version-specific packman pin in `deps/usd-lib-deps.json`)

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
  USD/Python imports it pulls from `deps/usd-deps.generated.packman.xml`, plus
  USD-version-specific libraries from `deps/usd-lib-deps.generated.packman.xml`).
  Build-host-only and developer tooling dependencies (`deps/host-deps.packman.xml`,
  `deps/repo-deps.packman.xml`, and the optional `deps/repo-deps-nv.packman.xml`
  side-car) are not included here because they are not redistributed with the product.
* **`omnimesh_ops_usd`, `autouv-core`, and `shrinkwrap_openvdb`** are pinned per OpenUSD
  version in `deps/usd-lib-deps.json` (currently 26.08 and 25.11). The entries above
  describe the default build selected by `repo.toml`'s `usd_ver` token; consult that file
  for the exact packman strings when building against another version.
* **The OpenUSD, oneTBB and MaterialX pins also vary by USD version.** From 26.08 oneTBB and
  MaterialX are their own packman packages; through 25.11 both ship inside the OpenUSD
  package. The pins a given build actually resolved are in
  `deps/usd-deps.generated.packman.xml`. These packages and the `python` package keep the
  license files of what they bundle in a `PACKAGE-LICENSES/` directory, which is not staged
  into the drop, so for the drop this file is the attribution of record.
* **Alembic, Imath, OpenSubdiv, hwloc and the separate zlib library ship only in USD 25.11
  drops.** They come with the OpenUSD 25.11 package; the 26.08 packages do not contain them.
* **Python (CPython, PSF License)** is pulled in via `deps/usd-deps.generated.packman.xml`. Only
  its runtime library is redistributed (see the CPython entry above). The `python` package's
  OpenSSL, libffi and SQLite live in extension modules that the drops do not ship.
* For each NVIDIA-distributed package above, the canonical and most up-to-date copyright,
  license, and embedded third-party notices are the ones shipped inside that package
  (typically as `LICENSE`, `LICENSE.txt`, or `THIRD_PARTY_NOTICES`). The entries here are a
  product-level summary intended to satisfy the OSS attribution requirement at the level
  of the Usd Optimize Core distribution.
