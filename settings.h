#ifndef SETTINGS_H
#define SETTINGS_H

// =============================================================================
//  Settings
//  -----------------------------------------------------------------------------
//  Centralised wrapper around QSettings, configured to write to an INI file
//  next to the exe (rather than the Windows registry). INI is preferred for
//  this app because:
//    – Operators can edit it with Notepad. Registry edits are scary.
//    – The INI is portable: copy it to a new install and you keep your
//      tuning. Registry settings don't move with the binary.
//    – Version control and on-disk diff become possible.
//
//  Anything here is meant to be user-tweakable via the Settings dialog.
//  Things that should never be tuned at runtime (e.g. internal cache sizes,
//  algorithmic constants) stay as compile-time constants in their owning
//  classes.
//
//  Defaults live in this file. If a key is missing from the INI on read,
//  the default is returned. That makes "delete the INI to reset" work.
// =============================================================================

#include <QSettings>
#include "textzoom.h"
#include <QString>
#include <QCoreApplication>

class Settings
{
public:
    // Build a QSettings pointing at <exe_dir>/dlconsole.ini. Each helper
    // below constructs its own — QSettings is cheap (it does its own
    // internal caching) and non-copyable, so we can't return one by
    // value from a static factory. Putting the path-building logic in
    // one place keeps the IsoFile location consistent across callers.
    static QString iniPath()
    {
        return QCoreApplication::applicationDirPath() + "/dlconsole.ini";
    }

    // ---- Network -----------------------------------------------------
    // UDP port to bind. Changes require a restart (the receiver is bound
    // to a port for its lifetime; rebinding mid-run is not implemented).
    static quint16 udpPort()             { QSettings s(iniPath(), QSettings::IniFormat); return s.value("network/udp_port", 50002).toUInt(); }
    static void    setUdpPort(quint16 p) { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("network/udp_port", p); }

    // Soft cap on cross-thread queue depth between receiver and dispatcher.
    static int  queueCapacity()        { QSettings s(iniPath(), QSettings::IniFormat); return s.value("network/queue_capacity", 50000).toInt(); }
    static void setQueueCapacity(int n){ QSettings s(iniPath(), QSettings::IniFormat); s.setValue("network/queue_capacity", n); }

    // ---- Per-tab model -----------------------------------------------
    static int  perTabCapacity()        { QSettings s(iniPath(), QSettings::IniFormat); return s.value("ui/per_tab_capacity", 200000).toInt(); }
    static void setPerTabCapacity(int n){ QSettings s(iniPath(), QSettings::IniFormat); s.setValue("ui/per_tab_capacity", n); }

    // ---- Disk log ----------------------------------------------------
    static QString diskLogRoot()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        const QString def = QCoreApplication::applicationDirPath() + "/LOGS";
        return s.value("disk/log_root", def).toString();
    }
    static void setDiskLogRoot(const QString &p)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("disk/log_root", p);
    }

    static qint64 diskRotationBytes()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("disk/rotation_bytes",
                       qint64(100 * 1024 * 1024)).toLongLong();
    }
    static void setDiskRotationBytes(qint64 n)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("disk/rotation_bytes", n);
    }

    // ---- Offline detection (2f) --------------------------------------
    static int  offlineWarnSeconds()         { QSettings s(iniPath(), QSettings::IniFormat); return s.value("ui/offline_warn_seconds", 10).toInt(); }
    static void setOfflineWarnSeconds(int n) { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("ui/offline_warn_seconds", n); }
    static int  offlineErrSeconds()          { QSettings s(iniPath(), QSettings::IniFormat); return s.value("ui/offline_err_seconds", 30).toInt(); }
    static void setOfflineErrSeconds(int n)  { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("ui/offline_err_seconds", n); }

    // Two-loco view (session 132): warn that the locos may not be on the same
    // section when their known location ranges are further apart than this.
    // 0 = whenever they do not overlap at all (the rule before session 132).
    static double twoLocoWarnApartKm()         { QSettings s(iniPath(), QSettings::IniFormat); return s.value("ui/two_loco_warn_apart_km", 10.0).toDouble(); }
    static void   setTwoLocoWarnApartKm(double k) { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("ui/two_loco_warn_apart_km", k); }

    // ---- Theme (2i) --------------------------------------------------
    // Master switch for continuous disk logging. When off, no .log and no
    // .dlr are written at all — the console is live-view only, and the
    // in-memory ring is the only history. Everything else in this group is
    // inert while this is false.
    static bool diskLoggingEnabled()
    { QSettings s(iniPath(), QSettings::IniFormat); return s.value("disk/enabled", true).toBool(); }
    static void setDiskLoggingEnabled(bool on)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("disk/enabled", on); }

    // Ceiling on the TOTAL size of the disk-log root, counting every .log
    // and .dlr under it. On reaching this, continuous logging suspends
    // itself rather than filling the partition; it resumes on its own if
    // space is freed. 0 means no ceiling.
    //
    // Default is 5 GiB. This is not a per-file limit — diskRotationBytes()
    // is what caps an individual file — it is the budget for the whole
    // archive, which matters because the archive grows without bound
    // otherwise and nothing else in the app deletes anything.
    static qint64 maxFolderBytes()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        bool ok = false;
        const qint64 v = s.value("disk/max_folder_bytes",
                                 qint64(5) * 1024 * 1024 * 1024).toLongLong(&ok);
        return (ok && v >= 0) ? v : qint64(5) * 1024 * 1024 * 1024;
    }
    static void setMaxFolderBytes(qint64 n)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("disk/max_folder_bytes", n); }

    // ---- column visibility ----------------------------------------------
    //
    // Which columns are shown in the per-source log tabs, as a list of
    // column indices.
    //
    // The default HIDES Source and Name. That is not a space-saving guess:
    // a tab is keyed by (source_id, kvchId), so within one tab those two
    // columns hold the same value on every single row — they repeat the tab
    // title, and the sidebar entry, once per message, for about 180px of
    // horizontal space that the Message column needs more. They remain
    // available from View › Columns for anyone who wants them, and they are
    // NOT hidden in the merged and search views, where rows genuinely come
    // from different sources and the columns carry information.
    static QList<int> visibleLogColumns()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        if (!s.contains("ui/visible_columns")) {
            return { 0, 3, 4, 5 };          // Time, Dir, Sev, Message
        }
        QList<int> out;
        for (const QString &v : s.value("ui/visible_columns").toStringList()) {
            bool ok = false;
            const int i = v.toInt(&ok);
            if (ok) out << i;
        }
        // An empty list would hide everything, leaving a table with no
        // columns and no obvious way back. Fall back rather than allow it.
        return out.isEmpty() ? QList<int>{ 0, 3, 4, 5 } : out;
    }
    static void setVisibleLogColumns(const QList<int> &cols)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        QStringList out;
        for (int c : cols) out << QString::number(c);
        s.setValue("ui/visible_columns", out);
    }

    // ---- column visibility ----------------------------------------------
    //
    // Indices of HIDDEN columns. Stored as the hidden set rather than the
    // visible one so that a column added in a future version defaults to
    // visible: an unknown index simply is not in the hidden list. Storing
    // the visible set would make every new column invisible for anyone with
    // an existing INI, which is the sort of thing nobody notices for weeks.
    static QList<int> hiddenColumns()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        QList<int> out;
        for (const QString &v : s.value("ui/hidden_columns").toStringList()) {
            bool ok = false;
            const int i = v.toInt(&ok);
            if (ok) out << i;
        }
        return out;
    }
    static void setHiddenColumns(const QList<int> &cols)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        QStringList out;
        for (int c : cols) out << QString::number(c);
        s.setValue("ui/hidden_columns", out);
    }

    // ---- time display ---------------------------------------------------
    //
    // false = local time (default), true = UTC.
    //
    // This exists because the two halves of the application disagreed: the
    // Time column rendered local time with no zone marker at all, while the
    // rolling .log and the .dlr both record UTC. Correlating a screenshot
    // against a log file therefore silently gave the wrong hour wherever
    // the operator's zone was not UTC, and doubly so across a DST change,
    // where local time is briefly ambiguous and unrecoverable.
    //
    // The fix is not to force one of them: control rooms think in local
    // time and archives must be zone-independent. The fix is to make the
    // display's zone explicit and switchable, so it can be matched to
    // whatever is being compared against.
    static bool showUtc()
    { QSettings s(iniPath(), QSettings::IniFormat); return s.value("ui/show_utc", false).toBool(); }
    static void setShowUtc(bool on)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("ui/show_utc", on); }

    // ---- row density ----------------------------------------------------
    //
    // 0 = Compact, 1 = Normal, 2 = Comfortable. Default is Normal.
    //
    // Density is ONE control rather than separate height and word-wrap
    // settings, because the two are not independent in a fixed-height
    // table: wrapping only shows the lines that fit, so "compact and
    // wrapped" silently means "compact", and offering it would be offering
    // a setting that does nothing. Each density therefore pairs a height
    // with the wrap behaviour that is actually legible at it.
    //
    // The old hard-coded 44px with wrapping is what Comfortable still is;
    // it fits roughly 15 rows on a 1080p screen, which is why it is no
    // longer the default.
    static int rowDensity()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        const int v = s.value("ui/row_density", 1).toInt();
        return (v >= 0 && v <= 2) ? v : 1;
    }
    static void setRowDensity(int d)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("ui/row_density", qBound(0, d, 2)); }

    // Pixel height for a density level.
    static int rowHeightFor(int density)
    {
        // At 100 % text size; scaled by TextZoom so bigger text is not
        // clipped by a fixed row.
        int base = 30;                          // Normal — single line, elided
        if (density == 0) {
            base = 22;                          // Compact — single line, elided
        } else if (density == 2) {
            base = 44;                          // Comfortable — wraps to ~2 lines
        }
        return qMax(12, qRound(base * TextZoom::factor()));
    }
    // Wrapping is only meaningful where more than one line fits.
    static bool wordWrapFor(int density) { return density == 2; }

    // ---- window layout --------------------------------------------------
    //
    // Stored as opaque blobs from QMainWindow::saveGeometry/saveState.
    //
    // kLayoutVersion is passed to saveState/restoreState and MUST be bumped
    // whenever a dock is added or removed. Qt matches docks by objectName;
    // restoring a state that names docks which no longer exist (or omits
    // ones that do) leaves the new dock in an arbitrary place, which reads
    // to the user as the layout being randomly broken. Bumping the version
    // makes Qt reject the stale blob and fall back to the code's defaults.
    // v2: added the Sources dock. Bumping this makes Qt reject layouts
    // saved before it existed, so the new dock lands where the code puts
    // it rather than in whatever gap the old arrangement left.
    static constexpr int kLayoutVersion = 2;

    static QByteArray mainWindowGeometry()
    { QSettings s(iniPath(), QSettings::IniFormat); return s.value("ui/geometry").toByteArray(); }
    static void setMainWindowGeometry(const QByteArray &b)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("ui/geometry", b); }

    static QByteArray mainWindowState()
    { QSettings s(iniPath(), QSettings::IniFormat); return s.value("ui/window_state").toByteArray(); }
    static void setMainWindowState(const QByteArray &b)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("ui/window_state", b); }

    // One shared column layout for every per-source tab: the columns are
    // identical across tabs, so storing widths per tab would mean the same
    // adjustment had to be made once per source.
    static QList<int> logColumnWidths()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        QList<int> out;
        for (const QString &v : s.value("ui/column_widths").toStringList()) {
            out << v.toInt();
        }
        return out;
    }
    static void setLogColumnWidths(const QList<int> &w)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        QStringList out;
        for (int v : w) out << QString::number(v);
        s.setValue("ui/column_widths", out);
    }

    // The tabs that were open when the program last closed, as
    // "key\x1fname\x1fvisible" records. Restored empty at startup so the
    // sources being watched are on screen before their first packet of the
    // day arrives — a console that starts blank every morning makes the
    // operator re-find the same four locos every morning.
    //
    // Filters are deliberately NOT part of this. A filter restored at
    // startup hides live traffic, and "why is nothing arriving" is a much
    // worse first minute than retyping a filter.
    static QStringList workspaceTabs()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("ui/workspace_tabs").toStringList();
    }
    static void setWorkspaceTabs(const QStringList &records)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("ui/workspace_tabs", records);
    }

    static QString workspaceActiveTab()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("ui/workspace_active", QString()).toString();
    }
    static void setWorkspaceActiveTab(const QString &key)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("ui/workspace_active", key);
    }

    static bool restoreWorkspace()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("ui/restore_workspace", true).toBool();
    }
    static void setRestoreWorkspace(bool on)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("ui/restore_workspace", on);
    }

    // Does Ctrl+F open Find as a floating window or as a bar in the tab?
    //
    // Defaults to the window, because that is what every editor does and
    // what people reach for. The choice follows the operator: detaching or
    // re-docking the bar sets this, so whichever they used last is what
    // Ctrl+F gives them next time.
    static bool findDetached()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("ui/find_detached", true).toBool();
    }
    static void setFindDetached(bool on)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("ui/find_detached", on);
    }

    // Which search mode Find opens in: 0 text, 1 extended, 2 regular
    // expression, 3 hex bytes, 4 query. Stored as the integer behind
    // FindBar::Mode rather than a name, and clamped on read — a mode that
    // no longer exists must not leave Find searching for nothing.
    static int findMode()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("ui/find_mode", 0).toInt();
    }
    static void setFindMode(int mode)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("ui/find_mode", mode);
    }

    // Does Next past the last match come back to the first?
    //
    // On by default, which is what every editor does. It is worth being able
    // to turn off here for a reason particular to a log: wrapping silently is
    // how an operator ends up reading the same three matches twice and
    // reporting them as six.
    static bool findWrap()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("ui/find_wrap", true).toBool();
    }
    static void setFindWrap(bool on)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("ui/find_wrap", on);
    }

    // How many rows one scan will walk before it stops. 0 means everything.
    // The cap exists so a pathologically large model cannot stall the GUI
    // thread; it is exposed because 200k is a guess, and an operator working
    // a two-million-row recording would rather wait than be told the answer
    // is "somewhere in the first tenth of it".
    static int findScanLimit()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("ui/find_scan_limit", 200000).toInt();
    }
    static void setFindScanLimit(int rows)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("ui/find_scan_limit", rows);
    }

    // Fields the operator has pinned, one "field<US>source" per entry.
    // Survives a restart because a pin is set up once and watched for the
    // length of a test run, not per session.
    static QStringList pinnedFields()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("ui/pinned_fields").toStringList();
    }
    static void setPinnedFields(const QStringList &pins)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("ui/pinned_fields", pins);
    }

    // Watches, one "expr<US>label<US>armed" per entry. The evidence is not
    // saved: a watch reloaded next session showing a hit from a run that has
    // ended would be read as a hit in this one.
    static QStringList watches()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("ui/watches").toStringList();
    }
    static void setWatches(const QStringList &w)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("ui/watches", w);
    }

    // Freeze the pinned-field board when a watch fires?
    static bool pinFreezeOnWatch()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("ui/pin_freeze_on_watch", false).toBool();
    }
    static void setPinFreezeOnWatch(bool on)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("ui/pin_freeze_on_watch", on);
    }

    // Is the advanced section of the Find window open? Purely a layout
    // memory, so the window comes back the size it was left.
    static bool findAdvancedOpen()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        return s.value("ui/find_advanced", false).toBool();
    }
    static void setFindAdvancedOpen(bool on)
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        s.setValue("ui/find_advanced", on);
    }

    // External kavach.xml to decode with. Empty means the copy compiled
    // into the binary, which is the shipping default — an external file is
    // for schema development, where the whole point is editing it between
    // reloads without rebuilding.
    static QString schemaPath()
    { QSettings s(iniPath(), QSettings::IniFormat); return s.value("schema/path", QString()).toString(); }
    static void setSchemaPath(const QString &p)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("schema/path", p); }

    // Free space to leave on the log volume. Default 1 GiB; 0 disables.
    static qint64 minFreeBytes()
    {
        QSettings s(iniPath(), QSettings::IniFormat);
        bool ok = false;
        const qint64 v = s.value("disk/min_free_bytes",
                                 qint64(1) * 1024 * 1024 * 1024).toLongLong(&ok);
        return (ok && v >= 0) ? v : qint64(1) * 1024 * 1024 * 1024;
    }
    static void setMinFreeBytes(qint64 n)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("disk/min_free_bytes", n); }

    // Whether the manual Save button writes a .dlr sidecar next to its .log.
    //
    // SEPARATE from rawCapture(), and deliberately NOT gated on
    // diskLoggingEnabled(). The two answer different questions:
    //
    //   rawCapture()      – should the CONTINUOUS writer record raw bytes
    //                       for every message, forever?
    //   saveIncludesRaw() – when the operator deliberately presses Save,
    //                       should the bytes come with it?
    //
    // Conflating them was wrong. Someone who turns continuous logging off
    // — for disk budget, or because the site does not want an always-on
    // recording — still wants the bytes on the one capture they chose to
    // keep, and that capture is exactly the one worth re-decoding later.
    // Raw bytes are held in memory unconditionally (see
    // MessageDispatcher::buildEntry), so this costs nothing until Save is
    // pressed.
    static bool saveIncludesRaw()
    { QSettings s(iniPath(), QSettings::IniFormat); return s.value("disk/save_includes_raw", true).toBool(); }
    static void setSaveIncludesRaw(bool on)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("disk/save_includes_raw", on); }

    // Byte-exact .dlr sidecar alongside each rolling .log. On by default:
    // the archive's stated purpose is post-hoc root-cause work, and a
    // text-only archive cannot be re-decoded. Operators on very constrained
    // storage can turn it off, at the cost of that capability.
    static bool rawCapture()
    { QSettings s(iniPath(), QSettings::IniFormat); return s.value("disk/raw_capture", true).toBool(); }
    static void setRawCapture(bool on)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("disk/raw_capture", on); }

    static QString theme()                    { QSettings s(iniPath(), QSettings::IniFormat); return s.value("ui/theme", "dark").toString(); }
    static void    setTheme(const QString &t) { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("ui/theme", t); }

    // Colour-blind-safe status colours (View > Theme). See UiColor.
    static bool colorBlindSafe()             { QSettings s(iniPath(), QSettings::IniFormat); return s.value("ui/colorBlindSafe", false).toBool(); }
    static void setColorBlindSafe(bool on)   { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("ui/colorBlindSafe", on); }

    // ---- crash recovery (session 79) -----------------------------------
    // True from startup until a clean close. Found true at the next start,
    // it means the last run ended without closeEvent: a crash, a kill, or
    // the power going.
    static bool sessionRunning()             { QSettings s(iniPath(), QSettings::IniFormat); return s.value("session/running", false).toBool(); }
    static void setSessionRunning(bool on)   { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("session/running", on); s.sync(); }

    // ---- RFID Tag Builder (session 201) --------------------------------
    // The scenario library folder: <exe_dir>/tag_scenarios unless set. The
    // copy built into the app is listed too, whether or not it exists.
    static QString tagLibraryPath()
    { QSettings s(iniPath(), QSettings::IniFormat); return s.value("tag_builder/library_path", QCoreApplication::applicationDirPath() + "/tag_scenarios").toString(); }
    static void setTagLibraryPath(const QString &p)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("tag_builder/library_path", p); }
    // The route open when the builder last opened one, reopened next time:
    // its file (a library ":/" path or a file on disk) and route index in it.
    static QString tagBuilderLastFile()
    { QSettings s(iniPath(), QSettings::IniFormat); return s.value("tag_builder/last_file").toString(); }
    static int tagBuilderLastRoute()
    { QSettings s(iniPath(), QSettings::IniFormat); return s.value("tag_builder/last_route", 0).toInt(); }
    static void setTagBuilderLast(const QString &file, int route)
    { QSettings s(iniPath(), QSettings::IniFormat); s.setValue("tag_builder/last_file", file); s.setValue("tag_builder/last_route", route); }
};

#endif // SETTINGS_H
