#include "brakeplant.h"

#include <QtGlobal>

BrakePlant::BrakePlant()
{
    // No brake: BP charged, BC empty, MR at 9.
    m_p[Bp].value = m_p[Bp].target = 5.0;
    m_p[Bc].value = m_p[Bc].target = 0.0;
    m_p[Mr].value = m_p[Mr].target = 9.0;
}

void BrakePlant::setTarget(int ch, double kgcm2) { if (valid(ch)) m_p[ch].target = qMax(0.0, kgcm2); }
double BrakePlant::target(int ch) const { return valid(ch) ? m_p[ch].target : 0; }
void BrakePlant::setValue(int ch, double kgcm2) { if (valid(ch)) m_p[ch].value = qMax(0.0, kgcm2); }
double BrakePlant::value(int ch) const { return valid(ch) ? m_p[ch].value : 0; }

void BrakePlant::setRates(int ch, double apply, double release)
{
    if (!valid(ch)) return;
    m_p[ch].apply = qMax(0.0, apply);
    m_p[ch].release = qMax(0.0, release);
}

double BrakePlant::applyRate(int ch) const { return valid(ch) ? m_p[ch].apply : 0; }
double BrakePlant::releaseRate(int ch) const { return valid(ch) ? m_p[ch].release : 0; }

void BrakePlant::setFault(int ch, Fault f, double param)
{
    if (!valid(ch)) return;
    m_p[ch].fault = f;
    m_p[ch].param = param;
}

BrakePlant::Fault BrakePlant::fault(int ch) const { return valid(ch) ? m_p[ch].fault : Fault::None; }
double BrakePlant::faultParam(int ch) const { return valid(ch) ? m_p[ch].param : 0; }

bool BrakePlant::applying(int ch) const
{
    if (!valid(ch)) return false;
    const P &p = m_p[ch];
    // BP brakes by falling; BC by rising; MR "applies" by charging (rising).
    return ch == Bp ? p.target < p.value : p.target > p.value;
}

void BrakePlant::step(double seconds)
{
    if (seconds <= 0) return;
    for (int ch = 0; ch < kChannels; ++ch) {
        P &p = m_p[ch];
        switch (p.fault) {
        case Fault::Stuck:
            continue;
        case Fault::Leak:
            p.value = qMax(0.0, p.value - p.param * seconds);
            continue;
        case Fault::Slow:
        case Fault::None:
            break;
        }
        double rate = applying(ch) ? p.apply : p.release;
        if (p.fault == Fault::Slow && p.param > 1) rate /= p.param;
        const double step = rate * seconds;
        if (p.value < p.target) p.value = qMin(p.target, p.value + step);
        else                    p.value = qMax(p.target, p.value - step);
    }
}

bool BrakePlant::settled(int ch) const
{
    if (!valid(ch)) return true;
    const P &p = m_p[ch];
    return p.fault == Fault::Stuck || p.fault == Fault::Leak || p.value == p.target;
}
