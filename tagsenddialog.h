#ifndef TAGSENDDIALOG_H
#define TAGSENDDIALOG_H

// =============================================================================
//  Send a tag (session 410) — Loco Simulator ▸ RFID ▸ Send a tag…:
//  LocoTcasSimulator's "SPAD scenario RFID interface". One tag sent now, by
//  hand, on reader 1 and / or reader 2, from its page_x and page_y, whatever
//  the loco is doing: a tag the route does not hold, one out of place, one
//  sent again.
//  -----------------------------------------------------------------------------
//  For each reader: a tick, page_x, page_y, and what the tag says (its name,
//  type, location, CRC-30). From the route fills every ticked reader's
//  page_x / page_y with a route tag's, as the simulator's route list does.
//  Send: each ticked reader's frame, as the simulator's buildTagPacket makes
//  it (LocoSim::simulatorTagFrame); reader 2's on port B when it has its own.
// =============================================================================

#include <QDialog>

class LocoSimWindow;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class StatusLine;

class TagSendDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TagSendDialog(LocoSimWindow *window);

    void setReader(int reader, bool on);                 // reader 1 or 2 ticked
    void setPages(int reader, const QString &pageX, const QString &pageY);
    void fillFromRoute(int routeRow);                    // the route's tag into every ticked reader
    void refreshRoute();                                 // the route's tags in From the route
    int  send();                                         // frames sent; failures said on the status line
    QString says(int reader) const;                      // what the reader's tag says
    StatusLine *status() const { return m_status; }

private:
    void showTag(int reader);

    LocoSimWindow *m_window = nullptr;
    QCheckBox     *m_on[2] = { nullptr, nullptr };
    QLineEdit     *m_pageX[2] = { nullptr, nullptr };
    QLineEdit     *m_pageY[2] = { nullptr, nullptr };
    QLabel        *m_says[2] = { nullptr, nullptr };
    QComboBox     *m_fromRoute = nullptr;
    QPushButton   *m_send = nullptr;
    StatusLine    *m_status = nullptr;
};

#endif  // TAGSENDDIALOG_H
