#pragma once

#include "library/types.h"

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace echo {

class TrackDialog : public QDialog {
    Q_OBJECT

public:
    explicit TrackDialog(const Track& track, const QPixmap& artwork, QWidget* parent = nullptr);
    [[nodiscard]] TagEdit edit() const;

private:
    QLineEdit* m_title = nullptr;
    QLineEdit* m_artist = nullptr;
    QLineEdit* m_album = nullptr;
    QLineEdit* m_albumArtist = nullptr;
    QSpinBox* m_trackNumber = nullptr;
    QSpinBox* m_year = nullptr;
    QPlainTextEdit* m_lyrics = nullptr;
    QByteArray m_artwork;
    QString m_mime = QStringLiteral("image/jpeg");
    bool m_artworkChanged = false;
    bool m_cue = false;
};

} // namespace echo
