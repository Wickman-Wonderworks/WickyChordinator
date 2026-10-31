#pragma once

namespace boilerplate::parameters
{
    // Stable parameter identifiers. Never change these after releasing a preset
    // or automation will no longer map to the correct controls.
    inline constexpr auto gain = "gain";
    inline constexpr auto bypass = "bypass";

    // AU ordering contracts: keep each released parameter's hint unchanged.
    // New parameters must use a hint higher than every previously released hint.
    // Hints do not migrate changes to an existing parameter's meaning or range.
    inline constexpr int gainVersionHint = 1;
    inline constexpr int bypassVersionHint = 1;
}
