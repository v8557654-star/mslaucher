#pragma once

#include "common.h"

#include <QObject>
#include <QString>

namespace mcl {

enum class SkinOperation {
    Upload,
    Reset
};

class SkinWorker final : public QObject {
    Q_OBJECT
public:
    SkinWorker(SkinOperation operation,
               Account account,
               QString filePath,
               QString variant);

public slots:
    void run();

signals:
    void finished(const QString &message);
    void failed(const QString &message);

private:
    SkinOperation operation_;
    Account account_;
    QString filePath_;
    QString variant_;
};

} // namespace mcl
