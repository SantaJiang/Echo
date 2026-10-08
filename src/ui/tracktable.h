#pragma once

#include "library/types.h"

#include <QAbstractTableModel>
#include <QTableView>

namespace echo {

class TrackModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit TrackModel(QObject* parent = nullptr);
    void setTracks(QVector<Track> tracks);
    void setPlayingId(qint64 id);
    [[nodiscard]] Track trackAt(int row) const;
    [[nodiscard]] QVector<qint64> ids() const;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<Track> m_tracks;
    qint64 m_playingId = 0;
};

class TrackTable : public QTableView {
    Q_OBJECT

public:
    explicit TrackTable(QWidget* parent = nullptr);
    void setTracks(QVector<Track> tracks);
    void setPlayingId(qint64 id);
    [[nodiscard]] int hoverRow() const { return m_hoverRow; }
    [[nodiscard]] TrackModel* trackModel() const { return m_model; }

signals:
    void tracksActivated(const QVector<qint64>& ids, int index);
    void playNextRequested(const QVector<qint64>& ids);
    void enqueueRequested(const QVector<qint64>& ids);
    void playlistRequested(const QVector<qint64>& ids);
    void infoRequested(qint64 id);
    void revealRequested(qint64 id);
    void removeRequested(const QVector<qint64>& ids);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void applyColumnWidths();
    TrackModel* m_model = nullptr;
    int m_hoverRow = -1;
};

} // namespace echo
