#ifndef REVIEWPACK_H
#define REVIEWPACK_H

// =============================================================================
//  ReviewPack (session 370) — what the test site saw, in one file.
//  -----------------------------------------------------------------------------
//  Testing happens at the site; review happens somewhere else. The reviewer
//  answers with a reply pack (session 384): the same format, the same
//  captures with the reviewer's notes added, the answer in place of "what to
//  look at", and `in_reply_to` naming the pack it answers.
//
//  Testing happens at the site; review happens somewhere else. What the site
//  sent was whatever someone remembered to attach: a .dlr without its notes,
//  a screenshot, a time typed in a chat. The reviewer then decoded with
//  another kavach.xml, could not see the test run's verdicts, and asked.
//
//  A review pack is one .zip (File ▸ Export review pack… at the site, File ▸
//  Open review pack… where it is read):
//
//    manifest.json             what is in it, from where, for what (below)
//    README.txt                the same, for a person without the program
//    summary.html              the same with the notes, the DMI's changes and the
//                              test run, for any browser (session 383)
//    captures/<name>.dlr       each chosen tab's raw frames, as Save writes them
//    captures/<name>.notes.json  that tab's bookmarks and notes (session 369)
//    testrun/<run>.testrun.json  the open test run, when there is one
//    config/kavach.xml         the schema the site decoded with
//    config/<file>             colour rules, names: what the caller adds
//    attachments/<file>        what the site adds: clips, photos (session 382)
//
//  The manifest says who and where (site, tester, loco), what to look at
//  (free text), the span, the program and Qt version, the OS, and for every
//  file its size and CRC-32. Opening checks each CRC: a damaged file is named,
//  not read as evidence.
//
//  The zip is plain: deflated entries, no zip64, so a pack stays under
//  4 GB (the writer refuses beyond that). Windows' Explorer, macOS and
//  `unzip` open it.
// =============================================================================

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "bookmarks.h"
#include "logentry.h"

namespace ReviewPack {

struct Capture {
    QString tabKey;            // "21_1"
    QString name;              // "Loco 1" (the tab's name), for the file name
    QVector<LogEntryPtr> rows; // in order; rows without raw bytes are left out
    QVector<Bookmark> marks;   // this tab's, any time: the span picks
};

struct Input {
    QString site, tester, loco;
    QString whatToLookAt;      // the site's own words, first in README.txt
    qint64  fromMs = 0, toMs = 0;   // 0, 0: everything
    QVector<Capture> captures;
    QString testRunPath;       // "" or a *.testrun.json
    QByteArray schemaXml;      // the kavach.xml in force
    QString schemaFrom;        // its file, or "built in"
    QVector<QPair<QString, QString>> config;   // (name in config/, local path)
    QStringList attachments;   // local files the site chose; one it cannot read refuses the pack
    qint64 createdMs = 0;      // 0: now
    QJsonObject inReplyTo;     // a reply: the answered pack's site, tester, created, what_to_look_at
};

struct Written {
    bool ok = false;
    QString error;
    QStringList files;         // as in the zip
    int records = 0;           // frames across every .dlr
    int notes = 0;
    qint64 bytes = 0;          // the .zip
};

Written write(const QString &zipPath, const Input &in);

struct Opened {
    bool ok = false;
    QString error;
    QString dir;               // where it was unpacked
    QStringList dlrs;          // absolute paths, in the manifest's order
    QString testRunPath;       // "" when the pack has none
    QString schemaPath;        // config/kavach.xml, or ""
    QStringList attachments;   // absolute paths of the undamaged attachments
    QJsonObject manifest;
    QStringList damaged;       // files whose CRC or size does not match
    bool reused = false;       // this pack was unpacked here before: its notes files were kept (session 381)
    QString summary() const;   // "Review pack from Site A (tester): 2 captures, 3 notes, a test run"
    bool isReply() const { return manifest.contains(QStringLiteral("in_reply_to")); }
};

// Unpack into `intoDir` (made if needed; files already there are replaced,
// except the notes files when the same pack was unpacked there before).
Opened open(const QString &zipPath, const QString &intoDir);

// The folder a pack is unpacked into by default: beside it,
// "<pack>_files" (Pack.zip → Pack_files).
QString defaultDir(const QString &zipPath);

// The file-name part of a tab's capture: "Loco 1" + "21_1" → "Loco_1_21_1".
QString captureBase(const Capture &c);

// What a reply to `pack` is made of: its schema and test run, and who it
// answers. The caller adds the captures and the reviewer's words.
Input replyBase(const Opened &pack);

// An attachment's name in the pack: "EB 14:03 (2).gif" → "EB_14_03_2_.gif".
QString attachmentName(const QString &localPath);

}  // namespace ReviewPack

#endif // REVIEWPACK_H
