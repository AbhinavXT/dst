#ifndef XLSXBOOK_H
#define XLSXBOOK_H

// =============================================================================
//  XlsxBook (session 208) — read and write the cell values of a .xlsx file.
//  -----------------------------------------------------------------------------
//  Enough for the station layout spreadsheets of the old Python tool
//  (DEBUGGING_TOOL_KAVACH, config/station/*.xlsx): sheet names and cell
//  values, nothing else (no styles, formulas, merged cells or widths).
//
//  Qt has no public zip API, and its private QZipReader is not in every
//  Qt install (Debian's qtbase5-dev leaves the private headers out), so
//  the zip is read here: stored and deflated entries (RFC 1951 inflate,
//  after zlib's puff.c). Written with stored entries only, which Excel,
//  LibreOffice and openpyxl/pandas all read. No zip64: a station file is
//  tens of kilobytes.
//
//  A cell is a QVariant: QString, qlonglong (a whole number) or double;
//  an invalid QVariant is an empty cell. Rows keep their trailing empties
//  trimmed.
// =============================================================================

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVector>

namespace XlsxBook {

using Row = QVector<QVariant>;

struct Sheet {
    QString name;
    QVector<Row> rows;
};

// Every sheet, in workbook order. Empty and *err set if the file is not a
// readable .xlsx.
QVector<Sheet> read(const QByteArray &xlsx, QString *err = nullptr);
QVector<Sheet> readFile(const QString &path, QString *err = nullptr);

// A .xlsx holding `sheets`. Sheet names must be unique, 1-31 characters.
QByteArray write(const QVector<Sheet> &sheets);
bool writeFile(const QString &path, const QVector<Sheet> &sheets, QString *err = nullptr);

// --- the pieces, for tests ---------------------------------------------------
// Raw deflate (no zlib header). *ok false on a corrupt stream.
QByteArray inflate(const QByteArray &raw, bool *ok);
quint32 crc32(const QByteArray &data);   // the zip / PNG CRC-32
QString columnName(int index);           // 0 -> "A", 26 -> "AA"

}  // namespace XlsxBook

#endif // XLSXBOOK_H
