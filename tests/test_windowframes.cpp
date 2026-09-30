#include "testutil.h"

#include "windowgeometry.h"

#include <QApplication>
#include <QDialog>
#include <QMainWindow>

// =============================================================================
//  Tool windows have a frame you can maximise.
//
//  Reported from use: the Decode Workbench, Packet Maker and fault panel had
//  no maximise button. Several of this program's real work surfaces are
//  QDialogs, and a QDialog does not get minimise/maximise on most window
//  managers. Setting Qt::Window — which is what they did — makes them
//  independent windows but does not add the buttons: the hint set is
//  separate, and nothing in the code said so.
// =============================================================================

TEST_SUITE(windowframes)
{
    // ---- the helper ---------------------------------------------------------
    {
        QDialog d;
        CHECK(!(d.windowFlags() & Qt::WindowMaximizeButtonHint),
              "a plain QDialog has no maximise hint — this is the bug");

        WindowGeometry::makeResizableWindow(&d);
        CHECK(d.windowFlags() & Qt::WindowMaximizeButtonHint, "the helper adds maximise");
        CHECK(d.windowFlags() & Qt::WindowMinimizeButtonHint, "and minimise");
        CHECK(d.windowFlags() & Qt::WindowCloseButtonHint,    "and keeps close");
        CHECK(d.windowFlags() & Qt::Window,
              "and makes it an independent window rather than a panel");

        // The one that actually mattered. Qt::Dialog is Qt::Window | 0x2, so
        // OR-ing in Qt::Window leaves the type as Dialog — a transient window
        // that most window managers will not maximise no matter what hints it
        // carries. The type bits have to be replaced.
        CHECK((d.windowFlags() & Qt::WindowType_Mask) == Qt::Window,
              "and its window TYPE is a plain window, not still a dialog");
    }

    // ---- it does not throw away what is already set -------------------------
    {
        QDialog d;
        d.setWindowFlags(d.windowFlags() | Qt::WindowStaysOnTopHint);
        WindowGeometry::makeResizableWindow(&d);
        CHECK(d.windowFlags() & Qt::WindowStaysOnTopHint,
              "existing flags survive — it adds, it does not replace");
        CHECK(d.windowFlags() & Qt::WindowMaximizeButtonHint, "and still adds maximise");
    }

    // ---- a QMainWindow is left no worse --------------------------------------
    {
        QMainWindow w;
        WindowGeometry::makeResizableWindow(&w);
        CHECK(w.windowFlags() & Qt::WindowMaximizeButtonHint,
              "a main window keeps its maximise hint through the call");
    }

    // ---- null is not a crash -------------------------------------------------
    {
        WindowGeometry::makeResizableWindow(nullptr);
        CHECK(true, "a null window is ignored rather than dereferenced");
    }
}
