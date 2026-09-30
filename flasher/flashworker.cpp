#include "flashworker.h"

#include <QMetaType>

FlashWorker::FlashWorker(QObject* parent)
    : QObject(parent)
{
}

void FlashWorker::registerMetaTypes()
{
    qRegisterMetaType<kflash::FlashConfig>("kflash::FlashConfig");
    qRegisterMetaType<kflash::Progress>("kflash::Progress");
    qRegisterMetaType<kflash::FlashResult>("kflash::FlashResult");
}

void FlashWorker::requestCancel()
{
    engine_.cancel();
}

void FlashWorker::flashCard(kflash::FlashConfig config)
{
    kflash::Callbacks callbacks;

    callbacks.log = [this](kflash::LogLevel level, const std::string& text) {
        emit logLine(static_cast<int>(level), QString::fromStdString(text));
    };

    callbacks.progress = [this](const kflash::Progress& progress) {
        emit progressChanged(progress);
    };

    callbacks.bitmap = [this](const std::vector<uint8_t>& bitmap, uint32_t totalBlocks) {
        QByteArray copy(reinterpret_cast<const char*>(bitmap.data()),
                        static_cast<int>(bitmap.size()));
        emit bitmapChanged(copy, totalBlocks);
    };

    const kflash::FlashResult result = engine_.run(config, callbacks);
    emit cardFinished(result);
}
