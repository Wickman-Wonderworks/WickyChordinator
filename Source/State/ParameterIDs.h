#pragma once

namespace boilerplate::parameters
{
    // Stable parameter identifiers. Never change these after releasing a preset
    // or automation will no longer map to the correct controls.
    inline constexpr auto gain = "gain";
    inline constexpr auto bypass = "bypass";

    // Bump this when the meaning or range of an existing AU parameter changes.
    inline constexpr int versionHint = 1;
}
