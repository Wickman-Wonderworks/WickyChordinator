#pragma once

namespace boilerplate::parameters
{
    // Stable parameter identifiers. Never change these after releasing a preset
    // or automation will no longer map to the correct controls.
    inline constexpr auto gain = "gain";
    inline constexpr auto bypass = "bypass";

    // Chord Settings Matrix.
    inline constexpr auto chordType = "chord_type";
    inline constexpr auto complexity = "complexity";
    inline constexpr auto voicing = "voicing";
    inline constexpr auto pitchOffset = "pitch_offset";

    inline constexpr int pitchOffsetMin = -12;
    inline constexpr int pitchOffsetMax = 12;

    // AU ordering contracts: keep each released parameter's hint unchanged.
    // New parameters must use a hint higher than every previously released hint.
    // Hints do not migrate changes to an existing parameter's meaning or range.
    inline constexpr int gainVersionHint = 1;
    inline constexpr int bypassVersionHint = 1;

    // Added after the v1 release, so they must stay above both v1 hints.
    inline constexpr int chordTypeVersionHint = 2;
    inline constexpr int complexityVersionHint = 3;
    inline constexpr int voicingVersionHint = 4;
    inline constexpr int pitchOffsetVersionHint = 5;
}
