#pragma once

#include <QObject>
#include <QString>

namespace echo {

class Scanner : public QObject {
    Q_OBJECT

public:
    Scanner(QString databasePath, QString artworkDirectory, QObject* parent = nullptr);

public slots:
    void scanRoot(qint64 rootId, const QString& path);

signals:
    void progress(qint64 rootId, int done, int total);
    void finished(qint64 rootId, bool changed);
    void warning(const QString& message);
    void retryRequested(qint64 rootId);
    void statistics(qint64 rootId, int parsedFiles);
    void failed(const QString& message);

private:
    QString m_databasePath;
    QString m_artworkDirectory;
};

} // namespace echo
