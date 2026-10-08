#include "ui/icons.h"
#include "ui/trackdialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

namespace echo {

TrackDialog::TrackDialog(const Track& track, const QPixmap& artwork, QWidget* parent)
    : QDialog(parent)
    , m_cue(!track.cuePath.isEmpty())
{
    setWindowTitle(QStringLiteral("歌曲信息"));
    setMinimumWidth(460);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    m_title = new QLineEdit(track.title, this);
    m_artist = new QLineEdit(track.artist, this);
    m_album = new QLineEdit(track.album, this);
    m_albumArtist = new QLineEdit(track.albumArtist, this);
    m_trackNumber = new QSpinBox(this);
    m_trackNumber->setRange(0, 999);
    m_trackNumber->setValue(track.trackNumber);
    m_year = new QSpinBox(this);
    m_year->setRange(0, 9999);
    m_year->setValue(track.year);
    m_lyrics = new QPlainTextEdit(track.lyrics, this);
    m_lyrics->setMinimumHeight(120);
    form->addRow(QStringLiteral("标题"), m_title);
    form->addRow(QStringLiteral("艺人"), m_artist);
    form->addRow(QStringLiteral("专辑"), m_album);
    form->addRow(QStringLiteral("专辑艺人"), m_albumArtist);
    form->addRow(QStringLiteral("曲目号"), m_trackNumber);
    form->addRow(QStringLiteral("年份"), m_year);
    form->addRow(QStringLiteral("歌词"), m_lyrics);
    auto* artRow = new QHBoxLayout();
    auto* art = new QLabel(this);
    art->setFixedSize(72, 72);
    art->setPixmap((artwork.isNull() ? placeholderCover(72) : artwork).scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto* pick = new QPushButton(QStringLiteral("更换封面"), this);
    artRow->addWidget(art);
    artRow->addWidget(pick);
    artRow->addStretch();
    layout->addLayout(form);
    layout->addLayout(artRow);
    if (m_cue) {
        auto* note = new QLabel(QStringLiteral("这是 CUE 分轨，标题和艺人会写回 CUE 文件。"), this);
        note->setWordWrap(true);
        layout->addWidget(note);
        m_album->setReadOnly(true);
        m_albumArtist->setReadOnly(true);
        pick->setEnabled(false);
    }
    auto* buttons = new QDialogButtonBox(this);
    buttons->addButton(QStringLiteral("保存"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(pick, &QPushButton::clicked, this, [this, art] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择封面"), {}, QStringLiteral("图片 (*.jpg *.jpeg *.png)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        m_artwork = file.readAll();
        m_artworkChanged = true;
        m_mime = path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
        QPixmap pixmap;
        pixmap.loadFromData(m_artwork);
        art->setPixmap(pixmap.scaled(72, 72, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    });
}

TagEdit TrackDialog::edit() const
{
    TagEdit result;
    result.title = m_title->text();
    result.artist = m_artist->text();
    result.album = m_album->text();
    result.albumArtist = m_albumArtist->text();
    result.trackNumber = m_trackNumber->value();
    result.year = m_year->value();
    result.editLyrics = true;
    result.lyrics = m_lyrics->toPlainText();
    result.editArtwork = m_artworkChanged;
    result.artwork = m_artwork;
    result.artworkMime = m_mime;
    return result;
}

} // namespace echo
