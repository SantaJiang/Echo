#pragma once

#include "library/library.h"
#include "playback/player.h"
#include "quick/client.h"
#include "quick/playbackview.h"
#include "quick/quickview.h"
#include "test_support/support.h"

#include <QGuiApplication>
#include <QString>
#include <QTemporaryDir>
#include <QVector>
#include <QWindow>

namespace echo::quicktest {

QStringList& issues();
void pump(int milliseconds);

struct Harness {
    QGuiApplication& app;
    QTemporaryDir& temp;
    Library& library;
    QString& error;
    const QVector<qint64>& ids;
    TagEdit& tags;
    const QVector<qint64>& otherIds;
    qint64 sourceId = 0;
    test::SilentOutput* probe = nullptr;
    AudioEngine& engine;
    Player& player;
    PlaybackView& playback;
    Client& client;
    QuickView& window;
    const QString& coverPath;
};

void verifyFading(QuickView& shell, Client& client, Player& player, Library& library);
int runLayoutContract(Harness& harness);
int runBackgroundTest(Harness& harness);
int runOptionalScenes(Harness& harness);
int runInteractiveSuite(Harness& harness);

} // namespace echo::quicktest

#ifdef Q_OS_MACOS
void postNativeTitlebarDoubleClick(QWindow* window);
#endif
