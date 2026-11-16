#pragma once

#include <QObject>
#include <QVariantMap>
#include <functional>
#include <QList>

namespace echo {

class DialogService : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const QVariantMap&)>;
    explicit DialogService(QObject* parent = nullptr);
    [[nodiscard]] QVariantMap current() const { return m_dialog; }
    [[nodiscard]] bool isOpen() const { return !m_dialog.isEmpty(); }
    void request(QVariantMap data, Reply reply = {});
    void answer(int token, const QVariantMap& answer);
    void askName(const QString& title, const QString& initial, const QString& accept, std::function<void(QString)> reply);
    void choosePath(const QString& title, const QString& mode, const QStringList& filters, std::function<void(QString)> reply);

signals:
    void changed();

private:
    struct Pending {
        QVariantMap data;
        Reply reply;
    };
    QVariantMap m_dialog;
    QList<Pending> m_pending;
    Reply m_reply;
    int m_token = 0;
};

} // namespace echo
