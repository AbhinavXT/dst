/* =========================================================================
 * flashworker.h  -  Qt5 bridge between the GUI and kflash::FlashEngine
 *
 * Usage (from the flasher window):
 *
 *   auto* thread = new QThread(this);
 *   auto* worker = new FlashWorker;            // no parent: it moves thread
 *   worker->moveToThread(thread);
 *   connect(thread, &QThread::finished, worker, &QObject::deleteLater);
 *   connect(worker, &FlashWorker::progressChanged, this, &FlasherWindow::onProgress);
 *   connect(worker, &FlashWorker::bitmapChanged,   blockMap, &BlockMapWidget::setBitmap);
 *   connect(worker, &FlashWorker::logLine,         this, &FlasherWindow::appendLog);
 *   connect(worker, &FlashWorker::cardFinished,    this, &FlasherWindow::onCardFinished);
 *   thread->start();
 *
 *   // one card:
 *   QMetaObject::invokeMethod(worker, "flashCard", Qt::QueuedConnection,
 *                             Q_ARG(kflash::FlashConfig, config));
 *
 *   // Abort button - call DIRECTLY, not queued (the worker thread is busy
 *   // inside run(); cancel() only sets an atomic flag):
 *   worker->requestCancel();
 *
 * The engine emits callbacks on the worker thread; the signals below cross
 * to the GUI thread through Qt's queued connections automatically.
 * ========================================================================= */
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include "flash_engine.h"

Q_DECLARE_METATYPE(kflash::FlashConfig)
Q_DECLARE_METATYPE(kflash::Progress)
Q_DECLARE_METATYPE(kflash::FlashResult)

class FlashWorker : public QObject
{
    Q_OBJECT
public:
    explicit FlashWorker(QObject* parent = nullptr);

    /* Thread-safe. Call directly from the GUI thread. */
    void requestCancel();

    /* Call once at startup (before any connect) so queued signals can carry
     * the engine structs. */
    static void registerMetaTypes();

public slots:
    /* Blocking for the length of one card; runs on the worker thread. */
    void flashCard(kflash::FlashConfig config);

signals:
    void logLine(int level, const QString& text);           /* kflash::LogLevel as int */
    void progressChanged(const kflash::Progress& progress);  /* throttled to ~20 Hz by the engine */
    void bitmapChanged(const QByteArray& bitmap, quint32 totalBlocks);
    void cardFinished(const kflash::FlashResult& result);

private:
    kflash::FlashEngine engine_;
};
