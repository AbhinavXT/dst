#ifndef PROFILEIO_H
#define PROFILEIO_H
// =============================================================================
//  profileio.h -- moving saved profiles between PCs (session 94)
//  ---------------------------------------------------------------------------
//  The Loco Configuration window's configurations (values + send targets)
//  and the Firmware Flasher's profiles (VCC address, images, tuning) are each
//  exported to a small JSON file and imported on another PC. This holds the
//  one rule both share: what happens when an imported name is already used.
// =============================================================================
#include <QString>
#include <functional>

enum class ImportClash {
    Replace,     // the imported one overwrites the existing one, in its place
    KeepBoth,    // the imported one is added as "Name (2)", "Name (3)", ...
    Skip         // the existing one is kept; the imported one is dropped
};

// `base` if free, else "base (2)", "base (3)", ... -- the first free one.
inline QString uniqueImportName(const QString &base, const std::function<bool(const QString &)> &taken)
{
    if (!taken(base)) return base;
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
        if (!taken(candidate)) return candidate;
    }
}

#endif // PROFILEIO_H
