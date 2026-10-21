#pragma once

#include "library/types.h"

#include "ui/dialogs.h"

class QLineEdit;
class QLabel;
class QSpinBox;

namespace echo {

class TrackDialog : public DialogShell {
    Q_OBJECT

public:
    explicit TrackDialog(const Track& track, const QPixmap& artwork, QWidget* parent = nullptr);
    [[nodiscard]] TagEdit edit() const;
    void setArtwork(const QPixmap& artwork);

private:
    QLabel* m_art = nullptr;
    QLineEdit* m_title = nullptr;
    QLineEdit* m_artist = nullptr;
    QLineEdit* m_album = nullptr;
    QLineEdit* m_albumArtist = nullptr;
    QSpinBox* m_trackNumber = nullptr;
    QSpinBox* m_year = nullptr;
    QByteArray m_artwork;
    QString m_mime = QStringLiteral("image/jpeg");
    bool m_artworkChanged = false;
    bool m_cue = false;
};

} // namespace echo
