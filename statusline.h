#ifndef STATUSLINE_H
#define STATUSLINE_H
// =====================================================================
//  statusline.{h,cpp} -- the one-line "what just happened" under a
//  dialog's controls.
//
//  There were thirty-five of these across thirteen files, each with its
//  own wording, its own colour, and its own idea of how long a message
//  should stay. The colours were unified by uicolors; the CONVENTIONS
//  were not, and those are what an operator actually reads:
//
//    * Some messages were success and some were failure, and both were
//      plain black text — the reader had to parse the sentence to find
//      out which.
//    * Nothing ever expired. "Sent 40 frames" sat on screen ten minutes
//      after the send finished, reading as current state.
//    * Failures were sometimes overwritten by the next routine message
//      before anyone had read them.
//
//  So: four verbs, and one rule about time.
//
//    say()   neutral progress          transient
//    ok()    it worked                 transient
//    warn()  it worked, but read this  STICKY
//    fail()  it did not work           STICKY
//
//  Transient messages clear themselves after a few seconds, because a
//  stale success is worse than no message: it describes a state that has
//  passed. Warnings and failures never clear on a timer — the operator
//  decides when they have read them, by doing the next thing.
//
//  Each verb also carries a glyph. Colour alone is not a signal: a
//  quarter of the contrast work in this program exists because text was
//  unreadable, and for a colour-blind reader a red sentence and a green
//  one are the same sentence.
// =====================================================================
#include <QLabel>
#include <QString>

class QTimer;

class StatusLine : public QLabel
{
    Q_OBJECT

public:
    explicit StatusLine(QWidget *parent = nullptr);

    // How long a transient message stays. Long enough to read a sentence,
    // short enough that it cannot be mistaken for current state.
    static int transientMs();

    enum class Kind { None, State, Info, Ok, Warn, Fail };

    // state() is the odd one, and the distinction it draws is the reason
    // this class has five verbs instead of four.
    //
    // The other four report an EVENT: something happened, here is how it
    // went. state() describes what the panel is showing right now — "Select
    // a row to decode it", "Paste a hex frame". That is not news, it is the
    // caption, and a caption must not expire: a panel that explains itself
    // for six seconds and then goes blank is worse than one that never
    // explained itself at all.
    void state(const QString &text);  // neutral description, sticky

    void say(const QString &text);    // neutral progress, transient
    void ok(const QString &text);     // succeeded, transient
    void warn(const QString &text);   // qualified, sticky
    void fail(const QString &text);   // failed, sticky

    void clear();

    // For the tests, and for a caller that needs to know whether the line
    // is currently reporting a problem.
    Kind kind() const { return m_kind; }
    bool isSticky() const
    {
        return m_kind == Kind::Warn || m_kind == Kind::Fail || m_kind == Kind::State;
    }

    // The message without its glyph, which is what a test or a copy
    // button wants.
    QString message() const { return m_message; }

private:
    void set(Kind k, const QString &text);
    void applyColour();

    Kind    m_kind = Kind::None;
    QString m_message;
    QTimer *m_expiry = nullptr;
};

#endif  // STATUSLINE_H
