// Standalone parity harness: lifts the exact bit logic added to
// schemadecoder.cpp (Cursor::take lsb-first, readTyped's double path,
// compositeFloat's mps/curveA) out of Qt so it can be run in the sandbox and
// diffed against engine.py's output for the same frames.
//
// Not part of the build. It exists to prove the C++ double read agrees with
// the validated Python engine, since there is no Qt compiler here.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

struct Cursor {                       // copy of Decoder::Cursor, lsb-first only
    const unsigned char *b; int pos; int limit; bool msb; int nbytes;
    uint32_t take(int n) {
        uint32_t v = 0;
        if (n <= 0) return 0;
        for (int i = 0; i < n; ++i) {
            if (pos < 0 || pos >= limit || (pos >> 3) >= nbytes) { ++pos; continue; }
            const int byte = pos >> 3, off = pos & 7;
            const int bit = msb ? ((b[byte] >> (7 - off)) & 1) : ((b[byte] >> off) & 1);
            if (msb) v = (v << 1) | bit;
            else if (i < 32) v |= uint32_t(bit) << i;
            ++pos;
        }
        return v;
    }
};

// readTyped(), type="double" branch
static double readDouble(Cursor &c) {
    const uint32_t lo = c.take(32);
    const uint32_t hi = c.take(32);
    const uint64_t bits = (uint64_t(hi) << 32) | uint64_t(lo);
    double d; std::memcpy(&d, &bits, sizeof(d));
    return d;
}

static std::string fmt(const char *f, double a, double b = 0) {
    char buf[256]; std::snprintf(buf, sizeof buf, f, a, b); return buf;
}

// compositeFloat()
static std::string mps(double v)    { return fmt("%.4f m/s  (%.1f km/h)", v, v * 3.6); }
static std::string curveA(double v) {
    if (v == 0.0) return fmt("%.6f", v);
    return fmt("%.6f  (decel %.3f m/s\xC2\xB2)", v, -1.0 / (2.0 * v));
}
static std::string prec3m(double v) { return fmt("%.3f m", v); }

int main() {
    // frame arrives on stdin as whitespace-separated hex bytes
    std::vector<unsigned char> f;
    unsigned x;
    while (std::scanf("%2x", &x) == 1) f.push_back((unsigned char)x);

    Cursor c{ f.data(), 0, int(f.size()) * 8, false, int(f.size()) };

    printf("target_location    %s\n", prec3m(readDouble(c)).c_str());
    printf("target_speed       %s\n", mps(readDouble(c)).c_str());
    printf("target_type        %u\n", c.take(8));
    for (int k = 0; k < 9; ++k) {
        const double A = readDouble(c), C = readDouble(c), s = readDouble(c),
                     e = readDouble(c), hi = readDouble(c), lo = readDouble(c);
        printf("seg[%d]             A=%s   C=%s   start=%s   end=%s   hi=%s   lo=%s\n",
               k, curveA(A).c_str(), prec3m(C).c_str(), prec3m(s).c_str(),
               prec3m(e).c_str(), mps(hi).c_str(), mps(lo).c_str());
    }
    printf("cursor ended at bit %d of %d\n", c.pos, int(f.size()) * 8);
    return 0;
}
