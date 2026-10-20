#include "ui/mainwindow.h"

#include "audio/engine.h"
#include "library/library.h"
#include "platform/platform.h"
#include "playback/player.h"
#include "ui/miniplayer.h"
#include "ui/icons.h"
#include "ui/pages.h"
#include "ui/playerbar.h"
#include "ui/queuepanel.h"
#include "ui/sidebar.h"
#include "ui/settingsdialog.h"
#include "ui/theme.h"
#include "ui/trackdialog.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QCloseEvent>
#include <QSystemTrayIcon>
#include <QSignalBlocker>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QToolButton>
#include <QTextEdit>
#include <QTimer>
#include <QWindow>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace echo {
namespace {

QString askName(QWidget* parent, const QString& title, const QString& initial, const QString& confirmLabel)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setAttribute(Qt::WA_NoSystemBackground);
    dialog.setAutoFillBackground(false);
    dialog.setModal(true);
    const Palette colors = palette();
    dialog.setStyleSheet(QStringLiteral(
        "QDialog { background: transparent; }"
        "QFrame#nameCard { background: %1; border: 1px solid rgba(255,255,255,0.12); border-radius: 14px; }"
        "QLabel#nameTitle { font-size: 17px; font-weight: 600; color: %2; background: transparent; }"
        "QLineEdit#nameField { background: %3; border: 1px solid rgba(255,255,255,0.14); border-radius: 8px; padding: 0 12px; color: %2; min-height: 36px; selection-background-color: %4; selection-color: white; }"
        "QLineEdit#nameField:focus { border: 1px solid rgba(255,255,255,0.28); }"
        "QPushButton#nameCancel { background: transparent; color: %2; border: 1px solid rgba(255,255,255,0.16); border-radius: 8px; min-width: 76px; min-height: 32px; padding: 0 14px; }"
        "QPushButton#nameCancel:hover { background: %5; }"
        "QPushButton#nameOk { background: %2; color: %3; border: none; border-radius: 8px; min-width: 76px; min-height: 32px; padding: 0 14px; font-weight: 600; }"
        "QPushButton#nameOk:hover { background: #ffffff; }")
                             .arg(colors.sidebar.name(), colors.text.name(), colors.window.name(), colors.accent.name(), colors.hover.name()));
    auto* outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* card = new QFrame(&dialog);
    card->setObjectName(QStringLiteral("nameCard"));
    card->setAttribute(Qt::WA_StyledBackground);
    card->setAutoFillBackground(false);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(14);
    auto* heading = new QLabel(title, card);
    heading->setObjectName(QStringLiteral("nameTitle"));
    auto* field = new QLineEdit(initial, card);
    field->setObjectName(QStringLiteral("nameField"));
    field->setPlaceholderText(QStringLiteral("名称"));
    field->selectAll();
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    buttons->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("取消"), card);
    cancel->setObjectName(QStringLiteral("nameCancel"));
    cancel->setCursor(Qt::PointingHandCursor);
    auto* ok = new QPushButton(confirmLabel, card);
    ok->setObjectName(QStringLiteral("nameOk"));
    ok->setCursor(Qt::PointingHandCursor);
    ok->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addWidget(heading);
    layout->addWidget(field);
    layout->addLayout(buttons);
    outer->addWidget(card);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(field, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
    dialog.setFixedWidth(360);
    dialog.adjustSize();
    if (parent) {
        const QRect frame = parent->frameGeometry();
        dialog.move(frame.center() - QPoint(dialog.width() / 2, dialog.height() / 2));
    }
    field->setFocus();
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    return field->text().trimmed();
}

QString formatText(int sourceRate, int sourceBits, bool sourceFloat, int deviceRate, bool exclusive)
{
    if (sourceRate <= 0) {
        return {};
    }
    const QString depth = sourceFloat ? QStringLiteral("%1-bit float").arg(sourceBits) : QStringLiteral("%1 bit").arg(sourceBits);
    QString text = QStringLiteral("%1 kHz · %2").arg(sourceRate / 1000.0, 0, 'f', 1).arg(depth);
    if (deviceRate > 0 && deviceRate != sourceRate) {
        text += QStringLiteral(" → %1 kHz").arg(deviceRate / 1000.0, 0, 'f', 1);
    }
    if (exclusive) {
        text += QStringLiteral(" · 独占");
    }
    return text;
}

class CaptionBar : public QWidget {
public:
    explicit CaptionBar(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("captionBar"));
        setFixedHeight(36);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 0, 6, 0);
        layout->setSpacing(2);
        auto* title = new QLabel(QStringLiteral("Echo"), this);
        title->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
        auto make = [this](const QString& text, const QString& tip) {
            auto* button = new QToolButton(this);
            button->setText(text);
            button->setToolTip(tip);
            button->setAutoRaise(true);
            button->setFixedSize(32, 28);
            button->setFocusPolicy(Qt::NoFocus);
            return button;
        };
        auto* minimize = make(QStringLiteral("—"), QStringLiteral("最小化"));
        auto* maximize = make(QStringLiteral("□"), QStringLiteral("最大化"));
        auto* close = make(QStringLiteral("×"), QStringLiteral("关闭"));
        layout->addWidget(title);
        layout->addStretch();
        layout->addWidget(minimize);
        layout->addWidget(maximize);
        layout->addWidget(close);
        connect(minimize, &QToolButton::clicked, this, [this] { window()->showMinimized(); });
        connect(maximize, &QToolButton::clicked, this, [this] {
            if (window()->isMaximized()) {
                window()->showNormal();
            } else {
                window()->showMaximized();
            }
        });
        connect(close, &QToolButton::clicked, this, [this] { window()->close(); });
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (window()->isMaximized()) {
                window()->showNormal();
            } else {
                window()->showMaximized();
            }
        }
    }
};

} // namespace

MainWindow::MainWindow(Library* library, Player* player, AudioEngine* engine, QWidget* parent)
    : QMainWindow(parent)
    , m_library(library)
    , m_player(player)
    , m_engine(engine)
{
    setWindowTitle(QStringLiteral("Echo"));
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
#ifndef Q_OS_MACOS
    setWindowFlag(Qt::FramelessWindowHint);
#endif
    setMinimumSize(1040, 640);
    resize(1180, 760);
    setAcceptDrops(true);
    polish(this);

    m_sidebar = new Sidebar(this);
    m_songs = new SongsPage(library, this);
    m_albums = new AlbumsPage(library, this);
    m_artists = new ArtistsPage(library, this);
    m_folders = new FoldersPage(library, this);
    m_playlist = new PlaylistPage(library, this);
    m_searchPage = new SearchPage(library, this);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_songs);
    m_stack->addWidget(m_albums);
    m_stack->addWidget(m_artists);
    m_stack->addWidget(m_folders);
    m_stack->addWidget(m_playlist);
    m_stack->addWidget(m_searchPage);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("search"));
    m_search->setPlaceholderText(QStringLiteral("搜索"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedSize(260, 32);
    m_search->addAction(icon(Icon::Search, echo::palette().secondary, 14), QLineEdit::LeadingPosition);

    m_queue = new QueuePanel(this);
    m_queue->hide();
    m_bar = new PlayerBar(this);

    auto* content = new QVBoxLayout();
#ifdef Q_OS_MACOS
    content->setContentsMargins(0, 22, 0, 0);
#else
    content->setContentsMargins(0, 12, 0, 0);
#endif
    m_heading = new QLabel(QStringLiteral("歌曲"), this);
    m_heading->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: 700;"));
    m_heading->setFixedHeight(36);
    auto* searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(32, 0, 24, 8);
    searchRow->setAlignment(Qt::AlignVCenter);
    searchRow->addWidget(m_heading, 0, Qt::AlignVCenter);
    searchRow->addStretch();
    searchRow->addWidget(m_search, 0, Qt::AlignVCenter);
    content->addLayout(searchRow);
    auto* middle = new QHBoxLayout();
    middle->addWidget(m_stack, 1);
    middle->addWidget(m_queue);
    content->addLayout(middle, 1);

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(m_sidebar);
    auto* divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("contentDivider"));
    divider->setFixedWidth(1);
    divider->setFrameShape(QFrame::NoFrame);
    divider->setAutoFillBackground(true);
    QPalette dividerPalette = divider->palette();
    dividerPalette.setColor(QPalette::Window, echo::palette().dark ? QColor(255, 255, 255, 70) : QColor(QStringLiteral("#8E8E93")));
    divider->setPalette(dividerPalette);
    auto* right = new QVBoxLayout();
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(0);
    right->addLayout(content, 1);
    right->addWidget(m_bar);
    body->addWidget(divider);
    body->addLayout(right, 1);
    auto* root = new QWidget(this);
    auto* column = new QVBoxLayout(root);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
#ifndef Q_OS_MACOS
    column->addWidget(new CaptionBar(root));
#endif
    column->addLayout(body, 1);
    setCentralWidget(root);

    m_mini = new MiniPlayer(this);
    m_settings = new SettingsDialog(m_player, this);
    qApp->setQuitOnLastWindowClosed(false);
#ifdef Q_OS_MACOS
    installMenuBarIcon(this, [this] { showMainWindow(); }, [this] { showSettings(); });
#else
    m_tray = new QSystemTrayIcon(this);
    m_tray->setObjectName(QStringLiteral("echoTray"));
    QIcon trayIcon = icon(Icon::Songs, echo::palette().accent, 22);
    trayIcon.setIsMask(true);
    m_tray->setIcon(trayIcon);
    m_tray->setToolTip(QStringLiteral("Echo"));
    auto* trayMenu = new QMenu(this);
    trayMenu->addAction(QStringLiteral("打开 Echo"), this, &MainWindow::showMainWindow);
    trayMenu->addAction(QStringLiteral("设置…"), this, &MainWindow::showSettings);
    trayMenu->addSeparator();
    trayMenu->addAction(QStringLiteral("退出 Echo"), qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showMainWindow();
    });
    m_tray->show();
#endif
    connect(m_sidebar, &Sidebar::settingsRequested, this, &MainWindow::showSettings);
    buildMenus();

    auto wire = [this](TrackTable* table, bool albumContext) {
        connect(table, &TrackTable::tracksActivated, this, [this, albumContext](const QVector<qint64>& ids, int index) {
            playVisible(ids, index, albumContext);
        });
        connect(table, &TrackTable::playNextRequested, this, [this](const QVector<qint64>& ids) { m_player->enqueueNext(ids); });
        connect(table, &TrackTable::enqueueRequested, this, [this](const QVector<qint64>& ids) { m_player->enqueue(ids); });
        connect(table, &TrackTable::playlistRequested, this, [this](const QVector<qint64>& ids) { addToPlaylist(ids); });
        table->setFavorites(m_library->favoriteIds());
        connect(m_library, &Library::favoritesChanged, table, [this, table] {
            table->setFavorites(m_library->favoriteIds());
        });
        connect(table, &TrackTable::favoriteRequested, this, [this](qint64 id) {
            m_library->setFavorites({id}, !m_library->isFavorite(id));
        });
        connect(table, &TrackTable::infoRequested, this, &MainWindow::showInfo);
        connect(table, &TrackTable::revealRequested, this, &MainWindow::reveal);
    };
    wire(m_songs->table(), false);
    wire(m_albums->detailTable(), true);
    wire(m_artists->table(), false);
    wire(m_folders->table(), true);
    wire(m_playlist->table(), false);
    wire(m_searchPage->table(), false);
    const QVector<TrackTable*> tables {m_songs->table(), m_albums->detailTable(), m_artists->table(),
                                       m_folders->table(), m_playlist->table(), m_searchPage->table()};
    QVector<int> restored;
    const QString savedColumns = m_library->setting(QStringLiteral("track_columns"));
    for (const QString& part : savedColumns.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = part.toInt(&ok);
        if (ok) {
            restored.push_back(value);
        }
    }
    if (restored.size() == 5) {
        for (TrackTable* table : tables) {
            table->setColumnWidths(restored);
        }
    }
    auto* columnSave = new QTimer(this);
    columnSave->setSingleShot(true);
    columnSave->setInterval(200);
    connect(columnSave, &QTimer::timeout, this, [this, columnSave] {
        const QString value = columnSave->property("widths").toString();
        if (!value.isEmpty()) {
            m_library->setSetting(QStringLiteral("track_columns"), value);
        }
    });
    for (TrackTable* table : tables) {
        connect(table, &TrackTable::columnWidthsChanged, this, [tables, table, columnSave](const QVector<int>& widths) {
            QStringList parts;
            parts.reserve(widths.size());
            for (const int width : widths) {
                parts.push_back(QString::number(width));
            }
            columnSave->setProperty("widths", parts.join(QLatin1Char(',')));
            for (TrackTable* other : tables) {
                if (other != table) {
                    other->setColumnWidths(widths);
                }
            }
            columnSave->start();
        });
    }
    connect(m_songs->table(), &TrackTable::removeRequested, this, [this](const QVector<qint64>& ids) {
        if (QMessageBox::question(this, QStringLiteral("移除"), QStringLiteral("从资料库移除所选歌曲？文件不会被删除。")) == QMessageBox::Yes) {
            m_library->removeFromLibrary(ids);
        }
    });
    connect(m_playlist->table(), &TrackTable::removeRequested, this, [this](const QVector<qint64>& ids) {
        if (m_playlist->playlistId() == m_library->favoritesPlaylistId()) {
            m_library->setFavorites(ids, false);
            return;
        }
        QVector<int> positions;
        for (const QModelIndex& row : m_playlist->table()->selectionModel()->selectedRows()) {
            positions.push_back(row.row());
        }
        m_library->removePlaylistPositions(m_playlist->playlistId(), positions);
    });

    connect(m_sidebar, &Sidebar::librarySelected, this, &MainWindow::showLibraryPage);
    connect(m_sidebar, &Sidebar::playlistSelected, this, [this](qint64 id) {
        m_playlistId = id;
        m_playlist->setPlaylist(id);
        m_search->clear();
        m_heading->setText(m_playlist->findChild<QLabel*>(QStringLiteral("playlistTitle"))
                               ? m_playlist->findChild<QLabel*>(QStringLiteral("playlistTitle"))->text()
                               : QStringLiteral("歌单"));
        m_heading->show();
        m_stack->setCurrentWidget(m_playlist);
    });
    connect(m_sidebar, &Sidebar::createPlaylistRequested, this, &MainWindow::promptPlaylist);
    connect(m_sidebar, &Sidebar::createFolderRequested, this, &MainWindow::promptFolder);
    connect(m_sidebar, &Sidebar::renamePlaylistRequested, this, [this](qint64 id) {
        QString current;
        for (const Playlist& playlist : m_library->playlists()) {
            if (playlist.id == id) {
                current = playlist.name;
                break;
            }
        }
        const QString name = askName(this, QStringLiteral("重命名歌单"), current, QStringLiteral("完成"));
        if (!name.isEmpty()) {
            m_library->renamePlaylist(id, name);
        }
    });
    connect(m_sidebar, &Sidebar::renameFolderRequested, this, [this](qint64 id) {
        QString current;
        for (const PlaylistFolder& folder : m_library->playlistFolders()) {
            if (folder.id == id) {
                current = folder.name;
                break;
            }
        }
        const QString name = askName(this, QStringLiteral("重命名文件夹"), current, QStringLiteral("完成"));
        if (!name.isEmpty()) {
            m_library->renamePlaylistFolder(id, name);
        }
    });
    connect(m_sidebar, &Sidebar::deletePlaylistRequested, this, [this](qint64 id) { m_library->deletePlaylist(id); });
    connect(m_sidebar, &Sidebar::deleteFolderRequested, this, [this](qint64 id) { m_library->deletePlaylistFolder(id); });
    connect(m_sidebar, &Sidebar::addFolderRequested, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("添加音乐文件夹"));
        if (!path.isEmpty()) {
            m_library->addRoot(path);
        }
    });
    connect(m_songs, &SongsPage::addFolderRequested, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("添加音乐文件夹"));
        if (!path.isEmpty()) {
            m_library->addRoot(path);
        }
    });

    connect(m_albums, &AlbumsPage::albumPlayRequested, this, [this](const QString& key, bool shuffle) {
        const auto tracks = m_library->albumTracks(key);
        QVector<qint64> ids;
        for (const Track& track : tracks) {
            ids.push_back(track.id);
        }
        if (shuffle) {
            m_player->setShuffle(true);
        }
        playVisible(ids, 0, true);
    });
    connect(m_albums, &AlbumsPage::artistRequested, this, [this](const QString& key) {
        m_artists->reload();
        m_artists->selectArtist(key);
        m_sidebar->setSelectedLibrary(2);
    });
    connect(m_artists, &ArtistsPage::albumPlayRequested, this, [this](const QString& key, bool) {
        m_albums->openAlbum(key);
        m_sidebar->setSelectedLibrary(1);
    });
    connect(m_folders, &FoldersPage::playFolderRequested, this, [this](const QString& folder) {
        const auto tracks = m_library->folderTracks(folder, true);
        QVector<qint64> ids;
        for (const Track& track : tracks) {
            ids.push_back(track.id);
        }
        playVisible(ids, 0, true);
    });
    connect(m_searchPage, &SearchPage::albumSelected, this, [this](const QString& key) {
        m_search->clear();
        m_albums->openAlbum(key);
        m_sidebar->setSelectedLibrary(1);
    });
    connect(m_searchPage, &SearchPage::artistSelected, this, [this](const QString& key) {
        m_search->clear();
        m_artists->reload();
        m_artists->selectArtist(key);
        m_sidebar->setSelectedLibrary(2);
    });
    connect(m_searchPage, &SearchPage::playlistSelected, this, [this](qint64 id) {
        m_search->clear();
        m_playlistId = id;
        m_playlist->setPlaylist(id);
        for (const auto& playlist : m_library->playlists()) {
            if (playlist.id == id) m_heading->setText(playlist.name);
        }
        m_heading->show();
        m_stack->setCurrentWidget(m_playlist);
    });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.trimmed().isEmpty()) {
            m_showingSearch = false;
            m_heading->show();
            m_stack->setCurrentIndex(m_playlistId > 0 && m_stack->currentWidget() == m_searchPage ? 4 : m_libraryPage);
            if (m_stack->currentWidget() == m_playlist) {
                m_playlist->reload();
            }
            return;
        }
        m_showingSearch = true;
        m_heading->hide();
        m_searchPage->search(text.trimmed());
        m_stack->setCurrentWidget(m_searchPage);
    });

    connect(m_bar, &PlayerBar::toggleRequested, m_player, &Player::toggle);
    connect(m_bar, &PlayerBar::nextRequested, m_player, &Player::next);
    connect(m_bar, &PlayerBar::previousRequested, m_player, &Player::previous);
    connect(m_bar, &PlayerBar::seekRequested, m_player, &Player::seek);
    connect(m_bar, &PlayerBar::volumeChanged, m_player, &Player::setVolume);
    connect(m_bar, &PlayerBar::shuffleToggled, this, [this] { m_player->setShuffle(!m_player->shuffle()); });
    connect(m_bar, &PlayerBar::repeatToggled, this, [this] {
        m_player->setRepeat(m_player->repeat() == Player::Repeat::Off ? Player::Repeat::All : Player::Repeat::Off);
    });
    connect(m_bar, &PlayerBar::queueToggled, this, [this] { m_queue->setVisible(!m_queue->isVisible()); });

    connect(m_queue, &QueuePanel::activated, this, [this](int index) {
        const auto queue = m_player->queue();
        if (index < 0 || index >= queue.size()) {
            return;
        }
        QVector<qint64> ids;
        for (const Player::Entry& entry : queue) {
            ids.push_back(entry.trackId);
        }
        m_player->playTracks(ids, index, queue[index].albumContext);
    });
    connect(m_queue, &QueuePanel::removed, m_player, &Player::removeQueueRow);
    connect(m_queue, &QueuePanel::moved, m_player, &Player::moveQueue);
    connect(m_queue, &QueuePanel::cleared, m_player, &Player::clearUpcoming);

    connect(m_mini, &MiniPlayer::toggleRequested, m_player, &Player::toggle);
    connect(m_mini, &MiniPlayer::nextRequested, m_player, &Player::next);
    connect(m_mini, &MiniPlayer::previousRequested, m_player, &Player::previous);
    connect(m_mini, &MiniPlayer::expandRequested, this, [this] {
        showMainWindow();
    });

    auto updateFavoriteButtons = [this] {
        const auto track = m_player->currentTrack();
        const bool liked = m_library->isFavorite(track.id);
        const bool enabled = track.id > 0 && m_library->track(track.id).id > 0;
        m_bar->setFavorite(liked, enabled);
        m_mini->setFavorite(liked, enabled);
    };
    auto toggleCurrentFavorite = [this] {
        const qint64 id = m_player->currentTrack().id;
        if (id > 0) m_library->setFavorites({id}, !m_library->isFavorite(id));
    };
    connect(m_bar, &PlayerBar::favoriteRequested, this, toggleCurrentFavorite);
    connect(m_mini, &MiniPlayer::favoriteRequested, this, toggleCurrentFavorite);
    connect(m_library, &Library::favoritesChanged, this, updateFavoriteButtons);
    connect(m_library, &Library::favoritesFailed, this, [this](const QString& message) {
        QMessageBox::warning(this, QStringLiteral("收藏失败"), message);
    });
    connect(m_library, &Library::changed, this, &MainWindow::refreshViews);
    connect(m_library, &Library::scanProgress, this, [this](int done, int total) {
        if (total <= 0 || done >= total) {
            return;
        }
        m_sidebar->setScanText(QStringLiteral("正在扫描 %1 / %2").arg(done).arg(total));
    });
    connect(m_library, &Library::scanFinished, this, [this] { m_sidebar->setScanText({}); });
    connect(m_library, &Library::scanFailed, this, [this](const QString& message) { m_sidebar->setScanText(message); });
    connect(m_player, &Player::changed, this, &MainWindow::syncPlayer);
    connect(m_engine, &AudioEngine::errorOccurred, this, [this](const QString& message) { m_bar->setStatus(message); });
    connect(m_engine, &AudioEngine::exclusiveFallback, this, [this](const QString&) {
        m_bar->setStatus(QStringLiteral("设备被占用，已改用共享模式"));
    });
    connect(m_engine, &AudioEngine::deviceOpened, this, [this](int sourceRate, int sourceBits, bool sourceFloat, int deviceRate, bool exclusive) {
        m_bar->setFormatText(formatText(sourceRate, sourceBits, sourceFloat, deviceRate, exclusive));
    });

    installMediaIntegration(this, [this](MediaCommand command, qint64 argument) {
        switch (command) {
        case MediaCommand::Toggle:
            m_player->toggle();
            break;
        case MediaCommand::Next:
            m_player->next();
            break;
        case MediaCommand::Previous:
            m_player->previous();
            break;
        case MediaCommand::Seek:
            m_player->seek(argument);
            break;
        }
    });

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        const Track track = m_player->currentTrack();
        const QPixmap art = m_library->artwork()->pixmap(track.artworkHash, 720);
        m_bar->setPosition(m_player->positionMs(), track.durationMs);
        if (m_mini->isVisible()) {
            m_mini->setTrack(track, art, m_player->state() == AudioEngine::State::Playing);
            m_mini->setFavorite(m_library->isFavorite(track.id), track.id > 0 && m_library->track(track.id).id > 0);
        }
        updateSystemNowPlaying(track, art.toImage(), m_player->positionMs(), track.durationMs, m_player->state() == AudioEngine::State::Playing);
    });
    timer->start(250);

    refreshViews();
    syncPlayer();
    applyWindowChrome(this);
    installSidebarEffect(m_sidebar);
}

MainWindow::~MainWindow() = default;

void MainWindow::showMainWindow()
{
    m_mini->hide();
    if (isMinimized()) showNormal();
    else show();
    raise();
    activateWindow();
}

void MainWindow::showSettings()
{
    m_settings->show();
    m_settings->raise();
    m_settings->activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_player->saveSession();
    hide();
    event->ignore();
}

void MainWindow::openPaths(const QStringList& paths)
{
    QVector<qint64> ids;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            m_library->addRoot(info.absoluteFilePath());
        } else {
            ids += m_library->importFiles({info.absoluteFilePath()});
        }
    }
    if (!ids.isEmpty()) {
        playVisible(ids, 0, false);
    }
}

void MainWindow::buildMenus()
{
    auto* file = menuBar()->addMenu(QStringLiteral("文件"));
    file->addAction(QStringLiteral("添加音乐文件夹…"), QKeySequence(Qt::CTRL | Qt::Key_O), this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("添加音乐文件夹"));
        if (!path.isEmpty()) {
            m_library->addRoot(path);
        }
    });
    file->addAction(QStringLiteral("导入播放列表…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("导入播放列表"), {}, QStringLiteral("播放列表 (*.m3u *.m3u8)"));
        if (path.isEmpty()) {
            return;
        }
        qint64 id = m_playlistId;
        if (id <= 0 || id == m_library->favoritesPlaylistId()) {
            id = m_library->createPlaylist(QFileInfo(path).completeBaseName());
            m_playlistId = id;
        }
        QString error;
        if (!m_library->importM3u(id, path, &error)) {
            QMessageBox::warning(this, QStringLiteral("导入失败"), error);
        } else {
            m_playlist->setPlaylist(id);
            if (auto* title = m_playlist->findChild<QLabel*>(QStringLiteral("playlistTitle"))) {
                m_heading->setText(title->text());
            }
            m_heading->show();
            m_stack->setCurrentWidget(m_playlist);
        }
    });
    file->addAction(QStringLiteral("导出播放列表…"), this, [this] {
        if (m_playlistId <= 0) {
            return;
        }
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出播放列表"), {}, QStringLiteral("播放列表 (*.m3u8)"));
        if (path.isEmpty()) {
            return;
        }
        QString error;
        if (!m_library->exportM3u(m_playlistId, path, &error)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        }
    });

    auto* settings = file->addAction(QStringLiteral("设置…"), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, &MainWindow::showSettings);
    settings->setMenuRole(QAction::PreferencesRole);
    auto* quit = file->addAction(QStringLiteral("退出 Echo"), QKeySequence::Quit, qApp, &QCoreApplication::quit);
    quit->setMenuRole(QAction::QuitRole);

    auto* edit = menuBar()->addMenu(QStringLiteral("编辑"));
    edit->addAction(QStringLiteral("撤销"), QKeySequence::Undo, m_library, &Library::undo);
    edit->addAction(QStringLiteral("重做"), QKeySequence::Redo, m_library, &Library::redo);
    edit->addAction(QStringLiteral("新建歌单"), QKeySequence(Qt::CTRL | Qt::Key_N), this, [this] { promptPlaylist(0); });

    auto* control = menuBar()->addMenu(QStringLiteral("控制"));
    control->addAction(QStringLiteral("播放/暂停"), this, [this] { m_player->toggle(); });
    control->addAction(QStringLiteral("下一首"), QKeySequence(Qt::CTRL | Qt::Key_Right), m_player, &Player::next);
    control->addAction(QStringLiteral("上一首"), QKeySequence(Qt::CTRL | Qt::Key_Left), m_player, &Player::previous);
    control->addAction(QStringLiteral("随机播放"), this, [this] { m_player->setShuffle(!m_player->shuffle()); });
    control->addAction(QStringLiteral("重复"), this, [this] {
        m_player->setRepeat(static_cast<Player::Repeat>((static_cast<int>(m_player->repeat()) + 1) % 3));
    });
    control->addAction(QStringLiteral("迷你播放器"), this, [this] {
        hide();
        const Track track = m_player->currentTrack();
        m_mini->setTrack(track, m_library->artwork()->pixmap(track.artworkHash, 128), m_player->state() == AudioEngine::State::Playing);
        m_mini->show();
    });
    auto* exclusive = control->addAction(QStringLiteral("独占输出"));
    exclusive->setCheckable(true);
    exclusive->setChecked(m_player->exclusive());
    connect(exclusive, &QAction::toggled, m_player, &Player::setExclusive);
    connect(m_player, &Player::changed, exclusive, [this, exclusive] {
        const QSignalBlocker blocker(exclusive);
        exclusive->setChecked(m_player->exclusive());
    });
    auto* replay = control->addAction(QStringLiteral("ReplayGain"));
    replay->setCheckable(true);
    replay->setChecked(m_player->replayGainEnabled());
    connect(replay, &QAction::toggled, m_player, &Player::setReplayGainEnabled);

    auto* search = new QAction(QStringLiteral("搜索"), this);
    search->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    connect(search, &QAction::triggered, m_search, [this] { m_search->setFocus(); m_search->selectAll(); });
    addAction(search);
    auto* volumeUp = new QAction(this);
    volumeUp->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up));
    connect(volumeUp, &QAction::triggered, this, [this] { m_player->setVolume(std::min(1.f, m_player->volume() + 0.05f)); });
    addAction(volumeUp);
    auto* volumeDown = new QAction(this);
    volumeDown->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down));
    connect(volumeDown, &QAction::triggered, this, [this] { m_player->setVolume(std::max(0.f, m_player->volume() - 0.05f)); });
    addAction(volumeDown);
}

void MainWindow::refreshViews()
{
    m_songs->reload();
    m_albums->reload();
    m_artists->reload();
    m_folders->reload();
    m_playlist->reload();
    if (m_showingSearch) {
        m_searchPage->search(m_search->text().trimmed());
    }
    m_sidebar->setPlaylists(m_library->playlistFolders(), m_library->playlists());
    syncPlayer();
}

void MainWindow::showLibraryPage(int index)
{
    m_libraryPage = index;
    m_playlistId = 0;
    m_search->clear();
    const QStringList titles {QStringLiteral("歌曲"), QStringLiteral("专辑"), QStringLiteral("艺人"), QStringLiteral("文件夹")};
    m_heading->setText(index >= 0 && index < titles.size() ? titles.at(index) : QStringLiteral("歌曲"));
    m_heading->show();
    m_stack->setCurrentIndex(index);
}

void MainWindow::syncPlayer()
{
    const Track track = m_player->currentTrack();
    const QPixmap art = m_library->artwork()->pixmap(track.artworkHash, 160);
    m_bar->setTrack(track, art);
    const bool canFavorite = track.id > 0 && m_library->track(track.id).id > 0;
    m_bar->setFavorite(m_library->isFavorite(track.id), canFavorite);
    m_mini->setTrack(track, art, m_player->state() == AudioEngine::State::Playing);
    m_mini->setFavorite(m_library->isFavorite(track.id), canFavorite);
    if (m_engine->state() == AudioEngine::State::Stopped) {
        m_bar->setFormatText(formatText(track.sampleRate, track.bitDepth,
            track.codec.contains(QStringLiteral("_f32")) || track.codec.contains(QStringLiteral("_f64")), 0, false));
    }
    m_bar->setPlaying(m_player->state() == AudioEngine::State::Playing);
    m_bar->setVolume(m_player->volume());
    m_bar->setShuffle(m_player->shuffle());
    m_bar->setRepeat(static_cast<int>(m_player->repeat()));
    m_bar->setPosition(m_player->positionMs(), track.durationMs);
    m_queue->setQueue(m_player->queue(), m_player->index(), m_library);
    const qint64 id = track.id;
    const bool playing = m_player->state() == AudioEngine::State::Playing;
    m_songs->setPlayingId(id, playing);
    m_albums->setPlayingId(id, playing);
    m_artists->setPlayingId(id, playing);
    m_folders->setPlayingId(id, playing);
    m_playlist->setPlayingId(id, playing);
    m_searchPage->setPlayingId(id, playing);
    if (track.missing) {
        m_bar->setStatus(QStringLiteral("找不到文件"));
    }
}

void MainWindow::playVisible(const QVector<qint64>& ids, int index, bool albumContext)
{
    m_player->playTracks(ids, index, albumContext);
}

void MainWindow::addToPlaylist(const QVector<qint64>& ids)
{
    QMenu menu(this);
    const auto playlists = m_library->playlists();
    if (playlists.isEmpty()) {
        menu.addAction(QStringLiteral("新建歌单"), this, [this, ids] {
            const qint64 id = m_library->createPlaylist(QStringLiteral("新歌单"));
            m_library->addToPlaylist(id, ids);
        });
    }
    for (const Playlist& playlist : playlists) {
        menu.addAction(playlist.name, this, [this, ids, playlist] { m_library->addToPlaylist(playlist.id, ids); });
    }
    menu.exec(QCursor::pos());
}

void MainWindow::showInfo(qint64 id)
{
    Track track = m_library->track(id);
    if (track.id == 0) {
        return;
    }
    TrackDialog dialog(track, m_library->artwork()->pixmap(track.artworkHash, 160), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QString error;
    if (!m_library->saveTags(id, dialog.edit(), &error)) {
        QMessageBox::warning(this, QStringLiteral("无法保存"), error.isEmpty() ? QStringLiteral("写入标签失败") : error);
    }
}

void MainWindow::reveal(qint64 id)
{
    const Track track = m_library->track(id);
    if (!track.path.isEmpty()) {
        showInFileManager(track.path);
    }
}

void MainWindow::promptPlaylist(qint64 folderId)
{
    const QString name = askName(this, QStringLiteral("新建歌单"), QStringLiteral("新歌单"), QStringLiteral("创建"));
    if (!name.isEmpty()) {
        m_library->createPlaylist(name, folderId);
    }
}

void MainWindow::promptFolder()
{
    const QString name = askName(this, QStringLiteral("新建文件夹"), QStringLiteral("新文件夹"), QStringLiteral("创建"));
    if (!name.isEmpty()) {
        m_library->createPlaylistFolder(name);
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        auto* focus = QApplication::focusWidget();
        if (!qobject_cast<QLineEdit*>(focus) && !qobject_cast<QTextEdit*>(focus) && !qobject_cast<QPlainTextEdit*>(focus)) {
            m_player->toggle();
            event->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.push_back(url.toLocalFile());
        }
    }
    openPaths(paths);
}

} // namespace echo
