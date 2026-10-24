#ifndef BRAKEPLANT_H
#define BRAKEPLANT_H
// =============================================================================
//  BrakePlant (session 360) -- the air the Brake Test Bench pretends to have.
//
//  Brake pipe, brake cylinder and main reservoir, each moving toward a target
//  at its own rates: "apply" toward more braking (BP down, BC up, MR
//  charging), "release" back. The targets come from the BIU's command, the
//  brake relays or a hand on the dial (brakebenchwindow.h); the firmware's
//  malfunction check judges what the plant does with them, so the rates and
//  faults are what a test turns.
//
//  Faults, per pressure: stuck (does not move), slow (rates divided), leak
//  (falls at a rate whatever the target).
// =============================================================================

class BrakePlant
{
public:
    enum Channel { Bp = 0, Bc, Mr, kChannels };
    enum class Fault { None, Stuck, Slow, Leak };

    BrakePlant();

    void   setTarget(int ch, double kgcm2);
    double target(int ch) const;
    void   setValue(int ch, double kgcm2);     // a hand on the dial: jumps there
    double value(int ch) const;
    // kg/cm^2 per second, toward more braking / back.
    void   setRates(int ch, double apply, double release);
    double applyRate(int ch) const;
    double releaseRate(int ch) const;
    // Slow: `param` times slower. Leak: falls `param` kg/cm^2 per second.
    void   setFault(int ch, Fault f, double param = 0);
    Fault  fault(int ch) const;
    double faultParam(int ch) const;

    void step(double seconds);
    bool settled(int ch) const;                 // at its target (or stuck / leaking)
    // Is the move from value to target "apply" (more braking)?
    bool applying(int ch) const;

private:
    struct P {
        double value = 0, target = 0;
        double apply = 1.0, release = 1.0;
        Fault  fault = Fault::None;
        double param = 0;
    };
    bool valid(int ch) const { return ch >= 0 && ch < kChannels; }
    P m_p[kChannels];
};

#endif  // BRAKEPLANT_H
