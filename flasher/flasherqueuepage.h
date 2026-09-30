#ifndef FLASHERQUEUEPAGE_H
#define FLASHERQUEUEPAGE_H
// =============================================================================
//  flasherqueuepage.{h,cpp} -- page 0 of the Firmware Flasher (Main.dc.html)
//
//  Left column: where the image goes (target, delivery route) and whether the
//  run is ready (pre-flight). Right: the four cards with their images, the
//  selected row's detail strip, and the action bar with the Flash button.
//  One card is flashed per run (see flasherqueuemodel.h for why).
//
//  The page owns no flashing logic. It holds the operator's choices, keeps
//  the images hashed and current (re-hashing when a file changes on disk),
//  and asks flashercore whether the batch is ready. The window reads the
//  batch out of it when the Flash button is pressed.
// =============================================================================
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <QWidget>

#include "flashercore.h"

class FlasherQueueModel;
class QCheckBox;
class QComboBox;
class QFileSystemWatcher;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableView;
class QTimer;
class QVBoxLayout;
class StatusLine;

// -----------------------------------------------------------------------------
//  The "Delivery route" diagram: This PC -> VCC slot 1 -> Input/Output/Analog.
//  Cards not in the batch are drawn with a dashed outline.
// -----------------------------------------------------------------------------
class FlasherRouteDiagram : public QWidget
{
    Q_OBJECT
public:
    explicit FlasherRouteDiagram(QWidget *parent = nullptr);
    void setIncludedCards(const QSet<int> &cardTypes);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QSet<int> m_included;
};

class FlasherQueuePage : public QWidget
{
    Q_OBJECT
public:
    explicit FlasherQueuePage(QWidget *parent = nullptr);

    // ---- set by the window ---------------------------------------------
    void setMode(Flasher::Mode mode);

    // Replace the target and the images with the profile's defaults. Rows
    // with no default image are cleared.
    void applyProfile(const Flasher::FlashProfile &profile);

    // ---- read by the window --------------------------------------------
    QString vccIp() const;
    quint16 port() const;
    QString adapterDescription() const;
    FlasherQueueModel *model() const { return m_model; }

    Flasher::PreflightReport preflight() const;

    // The selected card as a one-entry batch. Only call when
    // preflight().ready is true.
    QVector<Flasher::BatchEntry> batchEntries() const;

    // Select the card Flash will send to (0 = none).
    void selectCard(int cardType);

    // Load an image into a card's row (used by the tests, by the profile and
    // by "Load images from folder"). Does not change the selection.
    void setImageForCard(int cardType, const QString &path);

signals:
    void flashRequested();
    // The IP or port was edited, so the window can keep the app bar and the
    // Flashing page's header in step.
    void targetChanged();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void refreshPreflight();
    void refreshAdapters();
    void onTableClicked(const QModelIndex &index);
    void onSelectionChanged();
    void onFileChangedOnDisk(const QString &path);
    void rehashChangedFiles();
    void loadImagesFromFolder();

private:
    QWidget *buildLeftColumn();
    QWidget *buildQueueColumn();
    void     browseForRow(int rowIndex);
    void     loadImageIntoRow(int rowIndex, const QString &path);
    void     loadImageAndSelect(int rowIndex, const QString &path);   // browse / drop
    void     updateDetailStrip();
    void     updateWatcher();
    void     restyle();
    void     preselectAdapterForTarget();

    // Adapter currently chosen in the picker, host byte order; zero = none.
    quint32  selectedAdapterIp() const;
    quint32  selectedAdapterMask() const;

    // ---- state -------------------------------------------------------------
    Flasher::Mode        m_mode = Flasher::Mode::Engineer;
    Flasher::FlashProfile m_profile;          // for SHA pins
    QHash<int, QString>  m_engineProblem;     // card type -> FlashEngine::validate() refusal
    QSet<QString>        m_pendingRehash;     // paths changed on disk, waiting for the debounce

    // ---- widgets -------------------------------------------------------------
    FlasherQueueModel   *m_model = nullptr;
    QTableView          *m_table = nullptr;
    QLineEdit           *m_ipEdit = nullptr;
    QSpinBox            *m_portSpin = nullptr;
    QComboBox           *m_adapterCombo = nullptr;
    FlasherRouteDiagram *m_route = nullptr;
    QVBoxLayout         *m_preflightLayout = nullptr;
    QFrame              *m_detailStrip = nullptr;
    QLabel              *m_detailTitle = nullptr;
    QLabel              *m_detailPath = nullptr;
    QLabel              *m_detailSha = nullptr;
    QLabel              *m_detailModified = nullptr;
    QLabel              *m_procedureNote = nullptr;
    QLabel              *m_blockerLabel = nullptr;
    QPushButton         *m_flashButton = nullptr;
    StatusLine          *m_status = nullptr;
    QFileSystemWatcher  *m_watcher = nullptr;
    QTimer              *m_rehashTimer = nullptr;
    QList<QFrame *>      m_cards;             // panels restyled on a theme change
    QList<QLabel *>      m_sectionLabels;
};

#endif  // FLASHERQUEUEPAGE_H
