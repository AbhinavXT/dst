#ifndef SUBPACKETWINDOW_H
#define SUBPACKETWINDOW_H

// =============================================================================
//  SubPacketWindow
//  -----------------------------------------------------------------------------
//  The sub-packet field editor, as its own window.
//
//  It used to live in the right half of a splitter inside the Packet Maker,
//  which meant a MovementAuthority's fields and a TagLinking's repeat table
//  were competing for a pane a few hundred pixels tall, inside a dialog that
//  also had to show the header form, the buffer box, the preview, the vary
//  table and two rows of buttons. Everything was scrollable and nothing was
//  readable. Sub-packets are also where the volume is — a repeat table wants
//  to be wide and tall, and it was getting neither.
//
//  So it detaches. One window, retargeted as the selection moves, rather than
//  one per sub-packet: a packet can carry eight, and eight stacked windows is
//  a worse problem than the one being solved.
//
//  It edits the Packet Maker's sub-packet vector IN PLACE through a pointer,
//  rather than holding a copy to be merged back on OK. A copy would need an
//  apply step, and an apply step is a thing to forget — the old inline editor
//  committed on every selection change precisely so nothing could be silently
//  lost, and that property is worth keeping. The pointer is only valid while
//  the parent dialog lives, which is guaranteed by making this a child of it.
// =============================================================================

#include <QDialog>
#include <QHash>
#include <QVector>

#include "schema/schemaencoder.h"

class QFormLayout;
class QLabel;
class QTableWidget;

class SubPacketWindow : public QDialog
{
    Q_OBJECT

public:
    explicit SubPacketWindow(QWidget *parent = nullptr);

    // Point the window at one sub-packet. `subs` must outlive the window.
    // Rebuilds the form for that sub-packet's struct.
    void setTarget(const Schema::Encoder *enc, const QString &captype,
                   QVector<Schema::SubEntry> *subs, int index);

    // Stop editing anything — used when the packet type changes or the list
    // is emptied, so the window cannot write through a stale index.
    void clearTarget();

    int  index() const { return m_index; }
    bool hasTarget() const { return m_subs && m_index >= 0; }

    // Read every widget back into the targeted sub-packet. Safe to call when
    // there is no target.
    void commit();

signals:
    // Emitted after commit() changes anything, so the caller can refresh the
    // list labels and invalidate the built frame.
    void edited();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void rebuild();

    const Schema::Encoder     *m_enc  = nullptr;
    QVector<Schema::SubEntry> *m_subs = nullptr;
    QString                    m_captype;
    int                        m_index = -1;

    QLabel      *m_title = nullptr;
    QWidget     *m_host  = nullptr;
    QFormLayout *m_form  = nullptr;

    QHash<QString, QWidget *>      m_editors;
    QHash<QString, QTableWidget *> m_repeatTables;
};

#endif  // SUBPACKETWINDOW_H
