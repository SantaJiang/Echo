#include "playback/loudnessservice.h"

#include "library/settingskeys.h"
#include "playback/tracksource.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace echo {

LoudnessService::LoudnessService(TrackSource* source)
    : m_source(source)
{
}

QString LoudnessService::key(const Track& track) const
{
    if (const auto it = m_keys.constFind(track.id); it != m_keys.cend())
        return it.value();
    const QFileInfo file(track.path);
    const QString source = QStringLiteral("r128-v1|%1|%2|%3|%4|%5")
                               .arg(track.path)
                               .arg(file.size())
                               .arg(file.lastModified().toMSecsSinceEpoch())
                               .arg(track.cueStartMs)
                               .arg(track.cueEndMs);
    const QString hashed = QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_keys.insert(track.id, hashed);
    return hashed;
}

LoudnessResult LoudnessService::cached(const Track& track) const
{
    const QString id = key(track);
    if (m_values.contains(id))
        return m_values.value(id);
    const auto json = QJsonDocument::fromJson(m_source->setting(settings::loudnessPrefix + id).toUtf8()).object();
    if (!json.contains(QStringLiteral("lufs")) || !json.contains(QStringLiteral("peak"))) {
        m_values.insert(id, {});
        return {};
    }
    LoudnessResult result { true, json.value(QStringLiteral("lufs")).toDouble(), json.value(QStringLiteral("peak")).toDouble() };
    m_values.insert(id, result);
    return result;
}

void LoudnessService::store(const QString& key, const LoudnessResult& result)
{
    m_values.insert(key, result);
}

void LoudnessService::markFailed(const QString& key)
{
    m_failed.insert(key);
}

bool LoudnessService::failed(const QString& key) const
{
    return m_failed.contains(key);
}

void LoudnessService::forgetTrack(qint64 id)
{
    m_keys.remove(id);
}

void LoudnessService::clearKeys()
{
    m_keys.clear();
}

} // namespace echo
