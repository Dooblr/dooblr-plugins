#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dooblr0coast::dsp
{
    /** Documented 0-Coast patch points. Outputs may fan out; each input takes at most one cable. */
    enum class OutJack : int8_t
    {
        MidiACv = 0,
        MidiAGate,
        MidiBCv,
        MidiBGate,
        Clock,
        Random,
        Math1,
        Math2,
        OscTri,
        OscSqr,
        SlopeEoc,
        SlopeCv,
        ContourEon,
        ContourCv,
        Dynamics,
        Count
    };

    enum class InJack : int8_t
    {
        Tempo = 0,
        MathCh1,
        MathCh2,
        Osc1VOct,
        OscLinFm,
        OvertoneCv,
        MultiplyCv,
        SlopeTime,
        SlopeTrig,
        ContourDecay,
        ContourGate,
        BalanceExt,
        BalanceCv,
        DynamicsCv,
        Count
    };

    struct Cable
    {
        OutJack from {};
        InJack to {};
    };

    inline constexpr int outCount = (int) OutJack::Count;
    inline constexpr int inCount  = (int) InJack::Count;

    inline const char* outJackName (OutJack jack)
    {
        static constexpr const char* names[] {
            "MIDI A CV", "MIDI A Gate", "MIDI B CV", "MIDI B Gate",
            "Clock", "Random", "Math 1", "Math 2",
            "Triangle", "Square", "EOC", "Slope",
            "EON", "Contour", "Dynamics"
        };
        const auto i = (int) jack;
        return i >= 0 && i < outCount ? names[i] : "?";
    }

    inline const char* inJackName (InJack jack)
    {
        static constexpr const char* names[] {
            "Tempo", "Math CH1", "Math CH2", "1V/Oct", "Lin FM",
            "Overtone CV", "Multiply CV", "Slope Time", "Slope Trig",
            "Decay CV", "Contour Gate", "Balance Ext", "Balance CV", "Dynamics CV"
        };
        const auto i = (int) jack;
        return i >= 0 && i < inCount ? names[i] : "?";
    }

    inline const char* outJackId (OutJack jack)
    {
        static constexpr const char* ids[] {
            "out_midiACv", "out_midiAGate", "out_midiBCv", "out_midiBGate",
            "out_clock", "out_random", "out_math1", "out_math2",
            "out_oscTri", "out_oscSqr", "out_slopeEoc", "out_slopeCv",
            "out_contourEon", "out_contourCv", "out_dynamics"
        };
        const auto i = (int) jack;
        return i >= 0 && i < outCount ? ids[i] : "out";
    }

    inline const char* inJackId (InJack jack)
    {
        static constexpr const char* ids[] {
            "in_tempo", "in_mathCh1", "in_mathCh2", "in_osc1VOct", "in_oscLinFm",
            "in_overtoneCv", "in_multiplyCv", "in_slopeTime", "in_slopeTrig",
            "in_contourDecay", "in_contourGate", "in_balanceExt", "in_balanceCv", "in_dynamicsCv"
        };
        const auto i = (int) jack;
        return i >= 0 && i < inCount ? ids[i] : "in";
    }

    /** Switching-jack normals from the manual's Default Sound / Overriding Normals. */
    inline bool hasNormal (InJack jack)
    {
        return jack == InJack::MultiplyCv
            || jack == InJack::ContourGate
            || jack == InJack::BalanceExt
            || jack == InJack::DynamicsCv
            || jack == InJack::MathCh2;
    }
}
