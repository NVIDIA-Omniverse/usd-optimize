// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <string>

namespace usd_optimize::physics_body_meshes
{

/// A rejected or skipped path and the issue that prevents processing it.
struct PathIssue
{
    std::string path;
    std::string issue;
};

} // namespace usd_optimize::physics_body_meshes
