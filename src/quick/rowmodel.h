#pragma once
#include <QAbstractListModel>
#include <QSet>
#include <QVariantMap>

namespace echo {
class RowModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit RowModel(QObject* parent = nullptr)
        : QAbstractListModel(parent)
    {
    }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    int count() const { return m_rows.size(); }
    QVariant data(const QModelIndex& index, int role) const override
    {
        return index.isValid() && index.row() >= 0 && index.row() < m_rows.size() && role == Qt::UserRole
            ? QVariant(get(index.row()))
            : QVariant();
    }
    QHash<int, QByteArray> roleNames() const override { return { { Qt::UserRole, "item" } }; }
    Q_INVOKABLE QVariantMap get(int row) const
    {
        if (row < 0 || row >= m_rows.size())
            return {};
        auto result = m_rows[row];
        const auto id = result.value("id").toLongLong();
        result["favorite"] = m_favorites.contains(id);
        result["playing"] = id > 0 && id == m_playingId;
        return result;
    }
    QString keyAt(int row) const
    {
        return row >= 0 && row < m_rows.size() ? m_rows[row].value("key").toString() : QString();
    }
    void setTrackState(const QSet<qint64>& favorites, qint64 playingId)
    {
        if (m_favorites == favorites && m_playingId == playingId)
            return;
        m_favorites = favorites;
        m_playingId = playingId;
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(m_rows.size() - 1), { Qt::UserRole });
    }
    void replace(QVector<QVariantMap> rows)
    {
        bool same = rows.size() == m_rows.size();
        for (int i = 0; same && i < rows.size(); ++i)
            same = rows[i].value("key") == m_rows[i].value("key");
        if (same) {
            for (int i = 0; i < rows.size(); ++i)
                if (rows[i] != m_rows[i]) {
                    m_rows[i] = std::move(rows[i]);
                    emit dataChanged(index(i), index(i), { Qt::UserRole });
                }
        } else {
            beginResetModel();
            m_rows = std::move(rows);
            endResetModel();
            emit countChanged();
        }
    }
    void setValue(int row, const QString& key, const QVariant& value)
    {
        if (row < 0 || row >= m_rows.size() || m_rows[row].value(key) == value)
            return;
        m_rows[row][key] = value;
        emit dataChanged(index(row), index(row), { Qt::UserRole });
    }
signals:
    void countChanged();

private:
    QVector<QVariantMap> m_rows;
    QSet<qint64> m_favorites;
    qint64 m_playingId = 0;
};
}
