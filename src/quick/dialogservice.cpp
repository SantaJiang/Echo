#include "quick/dialogservice.h"

#include <QUrl>

namespace echo {

DialogService::DialogService(QObject* parent)
    : QObject(parent)
{
}

void DialogService::request(QVariantMap data, Reply reply)
{
    data.insert(QStringLiteral("token"), ++m_token);
    if (!m_dialog.isEmpty()) {
        m_pending.append({ std::move(data), std::move(reply) });
        return;
    }
    m_dialog = std::move(data);
    m_reply = std::move(reply);
    emit changed();
}

void DialogService::answer(int token, const QVariantMap& answer)
{
    if (m_dialog.isEmpty() || m_dialog.value(QStringLiteral("token")).toInt() != token)
        return;
    auto reply = std::move(m_reply);
    m_dialog.clear();
    emit changed();
    if (reply)
        reply(answer);
    if (m_dialog.isEmpty() && !m_pending.isEmpty()) {
        auto next = m_pending.takeFirst();
        m_dialog = std::move(next.data);
        m_reply = std::move(next.reply);
        emit changed();
    }
}

void DialogService::askName(const QString& title, const QString& initial, const QString& accept,
    std::function<void(QString)> reply)
{
    request({ { QStringLiteral("kind"), QStringLiteral("name") }, { QStringLiteral("title"), title },
                { QStringLiteral("initial"), initial }, { QStringLiteral("accept"), accept } },
        [reply = std::move(reply)](const QVariantMap& answer) {
            if (answer.value(QStringLiteral("accepted")).toBool())
                reply(answer.value(QStringLiteral("name")).toString().trimmed());
        });
}

void DialogService::choosePath(const QString& title, const QString& mode, const QStringList& filters,
    std::function<void(QString)> reply)
{
    request({ { QStringLiteral("kind"), QStringLiteral("file") }, { QStringLiteral("title"), title },
                { QStringLiteral("mode"), mode }, { QStringLiteral("filters"), filters } },
        [reply = std::move(reply)](const QVariantMap& answer) {
            if (!answer.value(QStringLiteral("accepted")).toBool())
                return;
            const auto value = answer.value(QStringLiteral("path")).toString();
            const QUrl url(value);
            const auto path = url.isLocalFile() ? url.toLocalFile() : value;
            if (!path.isEmpty())
                reply(path);
        });
}

} // namespace echo
