#ifndef PORTEDIT_H
#define PORTEDIT_H

// =============================================================================
//  PortEdit
//  -----------------------------------------------------------------------------
//  A UDP port box: a QLineEdit that takes 1..65535, with QSpinBox's value()
//  / setValue() / valueChanged(int).
//
//  WHY NOT QSpinBox
//    The Port boxes were QSpinBox with NoButtons, 80 px wide. On the bench
//    PC (Windows 11, Qt 6.9: the windows11 style) they were drawn EMPTY --
//    the style still lays the spin buttons out beside the edit field, and in
//    80 px that leaves the number no room. The IP box beside it, a plain
//    QLineEdit, was fine. A line edit has no sub-controls for a style to get
//    wrong.
//
//  EDITING
//    valueChanged fires only for a number in range. Left empty (or 0), the
//    box goes back to the last good value when editing finishes, so value()
//    is always a port that can be sent to.
// =============================================================================

#include <QLineEdit>

class PortEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit PortEdit(QWidget *parent = nullptr);

    int value() const { return m_value; }
    void setValue(int port);

signals:
    void valueChanged(int port);

private:
    int m_value = 1;
};

#endif // PORTEDIT_H
