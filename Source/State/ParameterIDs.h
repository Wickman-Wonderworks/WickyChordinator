#pragma once

namespace boilerplate::parameters
{
    // Stable parameter identifiers. Never change these after releasing a preset
    // or automation will no longer map to the correct controls.
    inline constexpr auto gain = "gain";
    inline constexpr auto bypass = "bypass";
    inline constexpr auto power = "power";

    // Chord Settings Matrix.
    inline constexpr auto chordType = "chord_type";
    inline constexpr auto complexity = "complexity";
    inline constexpr auto voicing = "voicing";
    inline constexpr auto pitchOffset = "pitch_offset";

    // Global interaction engine.
    inline constexpr auto spread = "spread";
    inline constexpr auto spreadMode = "spread_mode";
    inline constexpr auto humanize = "humanize";
    inline constexpr auto noteOffMode = "note_off_mode";
    inline constexpr auto strum = "strum";
    inline constexpr auto strumMode = "strum_mode";

    // Tempo-synced strum (Phase 1). Free keeps the released percentage mapping;
    // Sync uses a musical division of the host tempo (or the manual fallback).
    inline constexpr auto strumTimingMode = "strum_timing_mode";
    inline constexpr auto strumDivision = "strum_division";
    inline constexpr auto strumFallbackBpm = "strum_fallback_bpm";

    // Phase 2 finite chord-ordering strategies. Legacy Direction obeys the
    // released Up/Down Strum Mode; the other patterns override it.
    inline constexpr auto strumPattern = "strum_pattern";

    inline constexpr int strumFallbackBpmMin = 20;
    inline constexpr int strumFallbackBpmMax = 300;
    inline constexpr int strumFallbackBpmDefault = 120;

    inline constexpr auto voicingStyle = "voicing_style";
    inline constexpr auto humanizeTiming = "humanize_timing";
    inline constexpr auto scaleRoot = "scale_root";
    inline constexpr auto scaleMode = "scale_mode";
    inline constexpr int voicingStyleVersionHint = 13;
    inline constexpr int humanizeTimingVersionHint = 14;
    inline constexpr int scaleRootVersionHint = 15;
    inline constexpr int scaleModeVersionHint = 16;

    // Phase 1 tempo-synced strum. Hints 17-19 are the first values above the
    // released contract (which ends at 16); keep them unique and ascending.
    inline constexpr int strumTimingModeVersionHint = 17;
    inline constexpr int strumDivisionVersionHint = 18;
    inline constexpr int strumFallbackBpmVersionHint = 19;

    // Phase 2 strum patterns. Hint 20 is the next unused value after Phase 1.
    inline constexpr int strumPatternVersionHint = 20;

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

    // Added for the 1000x580 interaction-engine expansion. All of these are
    // newer than every released hint above and must remain in ascending order.
    inline constexpr int powerVersionHint = 6;
    inline constexpr int spreadVersionHint = 7;
    inline constexpr int spreadModeVersionHint = 8;
    inline constexpr int humanizeVersionHint = 9;
    inline constexpr int noteOffModeVersionHint = 10;
    inline constexpr int strumVersionHint = 11;
    inline constexpr int strumModeVersionHint = 12;
}
