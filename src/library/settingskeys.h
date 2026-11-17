#pragma once

#include <QString>

namespace echo::settings {

inline const QString fading = QStringLiteral("fading");
inline const QString normalization = QStringLiteral("normalization");
inline const QString volume = QStringLiteral("volume");
inline const QString exclusive = QStringLiteral("exclusive");
inline const QString spatialAudio = QStringLiteral("spatial_audio");
inline const QString spatialDefault = QStringLiteral("spatial_default_v2");
inline const QString replayGain = QStringLiteral("replaygain");
inline const QString shuffle = QStringLiteral("shuffle");
inline const QString repeat = QStringLiteral("repeat");
inline const QString queue = QStringLiteral("queue");
inline const QString queueIndex = QStringLiteral("queue_index");
inline const QString replaceContextQueue = QStringLiteral("replace_context_queue");
inline const QString loudnessPrefix = QStringLiteral("loudness/");
inline const QString reduceMotion = QStringLiteral("quick_reduce_motion");
inline const QString trackColumns = QStringLiteral("track_columns");
inline const QString dynamicPlaybackBackground = QStringLiteral("dynamic_playback_background");
inline const QString glassSidebar = QStringLiteral("glass_sidebar");

} // namespace echo::settings
