#include "audio/decoder.h"

#include <core/engine/ffmpegpcm.h>

extern "C" {
#include <libavutil/replaygain.h>
}

#include <QByteArray>
#include <QVector>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/intreadwrite.h>
#include <libswresample/swresample.h>
}

namespace echo {
namespace {

QString ffmpegError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

bool parseDb(QString text, double* value)
{
    if (text.isEmpty()) {
        return false;
    }
    text.replace(QStringLiteral("dB"), QString(), Qt::CaseInsensitive);
    bool ok = false;
    const double parsed = text.trimmed().toDouble(&ok);
    if (!ok) {
        return false;
    }
    *value = parsed;
    return true;
}

void applyReplayGainValue(bool* hasGain, double* db, int32_t microbels)
{
    if (*hasGain || microbels == INT32_MIN) {
        return;
    }
    *db = static_cast<double>(microbels) / 100000.0;
    *hasGain = true;
}

void readSideReplayGain(const AVStream* stream, ReplayGain* gain)
{
    if (!stream || !stream->codecpar) {
        return;
    }
    const AVPacketSideData* side = nullptr;
    for (int index = 0; index < stream->codecpar->nb_coded_side_data; ++index) {
        if (stream->codecpar->coded_side_data[index].type == AV_PKT_DATA_REPLAYGAIN) {
            side = &stream->codecpar->coded_side_data[index];
            break;
        }
    }
    if (!side || side->size < static_cast<int>(sizeof(AVReplayGain))) {
        return;
    }
    const auto* replay = reinterpret_cast<const AVReplayGain*>(side->data);
    applyReplayGainValue(&gain->hasTrack, &gain->trackDb, replay->track_gain);
    applyReplayGainValue(&gain->hasAlbum, &gain->albumDb, replay->album_gain);
    if (replay->track_peak > 0) {
        gain->trackPeak = static_cast<double>(replay->track_peak) / 100000.0;
    }
    if (replay->album_peak > 0) {
        gain->albumPeak = static_cast<double>(replay->album_peak) / 100000.0;
    }
}

void readReplayGain(AVDictionary* dictionary, ReplayGain* gain)
{
    if (!dictionary) {
        return;
    }
    const auto value = [&](const char* key) {
        if (const auto* entry = av_dict_get(dictionary, key, nullptr, 0)) {
            return QString::fromUtf8(entry->value);
        }
        return QString();
    };
    if (!gain->hasTrack) {
        gain->hasTrack = parseDb(value("REPLAYGAIN_TRACK_GAIN"), &gain->trackDb);
    }
    if (!gain->hasAlbum) {
        gain->hasAlbum = parseDb(value("REPLAYGAIN_ALBUM_GAIN"), &gain->albumDb);
    }
    double peak = 0;
    if (parseDb(value("REPLAYGAIN_TRACK_PEAK"), &peak) && peak > 0) {
        gain->trackPeak = peak;
    }
    if (parseDb(value("REPLAYGAIN_ALBUM_PEAK"), &peak) && peak > 0) {
        gain->albumPeak = peak;
    }
    if (!gain->hasTrack) {
        bool ok = false;
        const double r128 = value("R128_TRACK_GAIN").toDouble(&ok);
        if (ok) {
            gain->trackDb = r128 / 256.0 + 5.0;
            gain->hasTrack = true;
        }
    }
    if (!gain->hasAlbum) {
        bool ok = false;
        const double r128 = value("R128_ALBUM_GAIN").toDouble(&ok);
        if (ok) {
            gain->albumDb = r128 / 256.0 + 5.0;
            gain->hasAlbum = true;
        }
    }
}

void readAppleGapless(AVDictionary* dictionary, qint64* delay, qint64* padding)
{
    if (!dictionary || (*delay > 0 && *padding > 0)) {
        return;
    }
    const auto* entry = av_dict_get(dictionary, "iTunSMPB", nullptr, 0);
    if (!entry) {
        return;
    }
    const auto parts = QString::fromUtf8(entry->value).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 3) {
        return;
    }
    bool okDelay = false;
    bool okPad = false;
    const auto priming = parts[1].toLongLong(&okDelay, 16);
    const auto trailing = parts[2].toLongLong(&okPad, 16);
    if (okDelay && *delay <= 0) {
        *delay = priming;
    }
    if (okPad && *padding <= 0) {
        *padding = trailing;
    }
}

} // namespace

struct Decoder::Impl {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* resampler = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    int streamIndex = -1;
    AVRational timeBase {};
    qint64 totalRaw = -1;
    qint64 nextRaw = 0;
    bool codecEof = false;
    bool sawFramePts = false;
    std::vector<uint8_t> pending;
    std::vector<uint8_t> overflow;
    bool rangeHold = false;
    int decodeErrors = 0;
    QString error;

    ~Impl()
    {
        reset();
    }

    void reset()
    {
        if (resampler) {
            swr_free(&resampler);
        }
        if (codec) {
            avcodec_free_context(&codec);
        }
        if (format) {
            avformat_close_input(&format);
        }
        av_packet_free(&packet);
        av_frame_free(&frame);
        pending.clear();
        overflow.clear();
        rangeHold = false;
        decodeErrors = 0;
        streamIndex = -1;
        codecEof = false;
        sawFramePts = false;
        nextRaw = 0;
        totalRaw = -1;
    }
};

Decoder::Decoder() = default;

Decoder::~Decoder()
{
    close();
}

void Decoder::close()
{
    m_impl.reset();
    m_format = {};
    m_gain = {};
    m_delay = 0;
    m_padding = 0;
    m_rangeStart = 0;
    m_rangeEnd = -1;
}

bool Decoder::isOpen() const
{
    return m_impl != nullptr;
}

bool Decoder::open(const QString& path, QString* error)
{
    close();
    m_impl = std::make_unique<Impl>();
    const QByteArray encoded = path.toUtf8();
    if (const int result = avformat_open_input(&m_impl->format, encoded.constData(), nullptr, nullptr); result < 0) {
        const QString message = ffmpegError(result);
        if (error) {
            *error = message;
        }
        close();
        return false;
    }
    if (const int result = avformat_find_stream_info(m_impl->format, nullptr); result < 0) {
        if (error) {
            *error = ffmpegError(result);
        }
        close();
        return false;
    }
    const int streamIndex = av_find_best_stream(m_impl->format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        if (error) {
            *error = QStringLiteral("文件里没有音频流");
        }
        close();
        return false;
    }
    AVStream* stream = m_impl->format->streams[streamIndex];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        if (error) {
            *error = QStringLiteral("没有可用的解码器");
        }
        close();
        return false;
    }
    m_impl->codec = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(m_impl->codec, stream->codecpar);
    m_impl->codec->pkt_timebase = stream->time_base;
    m_impl->codec->thread_count = 1;
    if (const int result = avcodec_open2(m_impl->codec, decoder, nullptr); result < 0) {
        if (error) {
            *error = ffmpegError(result);
        }
        close();
        return false;
    }

    AVSampleFormat packed = m_impl->codec->sample_fmt;
    if (av_sample_fmt_is_planar(packed)) {
        packed = av_get_packed_sample_fmt(packed);
    }
    if (packed == AV_SAMPLE_FMT_DBL || packed == AV_SAMPLE_FMT_NONE || packed == AV_SAMPLE_FMT_U8) {
        packed = AV_SAMPLE_FMT_FLT;
    }
    const bool needsConvert = packed != m_impl->codec->sample_fmt;
    if (needsConvert) {
        AVChannelLayout inputLayout = m_impl->codec->ch_layout;
        if (inputLayout.nb_channels <= 0) {
            av_channel_layout_default(&inputLayout, stream->codecpar->ch_layout.nb_channels > 0
                                                          ? stream->codecpar->ch_layout.nb_channels
                                                          : 2);
        }
        AVChannelLayout outputLayout;
        av_channel_layout_copy(&outputLayout, &inputLayout);
        if (const int result = swr_alloc_set_opts2(&m_impl->resampler, &outputLayout, packed, m_impl->codec->sample_rate,
                                                    &inputLayout, m_impl->codec->sample_fmt, m_impl->codec->sample_rate, 0,
                                                    nullptr);
            result < 0 || swr_init(m_impl->resampler) < 0) {
            av_channel_layout_uninit(&outputLayout);
            if (error) {
                *error = QStringLiteral("无法转换声道布局");
            }
            close();
            return false;
        }
        av_channel_layout_uninit(&outputLayout);
    }

    m_format.sampleRate = m_impl->codec->sample_rate;
    const AVCodecID codecId = stream->codecpar->codec_id;
    const bool dsd = codecId == AV_CODEC_ID_DSD_LSBF || codecId == AV_CODEC_ID_DSD_MSBF || codecId == AV_CODEC_ID_DSD_LSBF_PLANAR
        || codecId == AV_CODEC_ID_DSD_MSBF_PLANAR;
    if (dsd && m_format.sampleRate >= 8) {
        // FFmpeg's DSD decoder emits one PCM sample per byte, which is the bit rate divided by 8.
        m_format.sampleRate /= 8;
    }
    m_format.channels = m_impl->codec->ch_layout.nb_channels;
    m_format.channelMask = channelMask();
    const int rawBits = m_impl->codec->bits_per_raw_sample;
    m_format.pcm = Fooyin::Utils::sampleFormatFromFFmpeg(packed, rawBits);
    if (m_format.pcm == Fooyin::SampleFormat::Unknown) {
        m_format.pcm = Fooyin::SampleFormat::F32;
    }
    m_format.bitsPerSample = Fooyin::Utils::pcmBitDepth(m_format.pcm);
    m_format.bytesPerSample = Fooyin::Utils::pcmBytesPerSample(m_format.pcm);
    m_format.isFloat = m_format.pcm == Fooyin::SampleFormat::F32 || m_format.pcm == Fooyin::SampleFormat::F64;
    m_format.highAligned = m_format.pcm == Fooyin::SampleFormat::S24In32;
    if (!m_format.valid()) {
        if (error) {
            *error = QStringLiteral("音频格式无效");
        }
        close();
        return false;
    }

    m_impl->streamIndex = streamIndex;
    m_impl->timeBase = stream->time_base;
    m_impl->packet = av_packet_alloc();
    m_impl->frame = av_frame_alloc();
    if (stream->duration > 0) {
        m_impl->totalRaw = av_rescale_q(stream->duration, stream->time_base, AVRational {1, m_format.sampleRate});
    }
    m_delay = std::max(0, stream->codecpar->initial_padding);
    m_padding = std::max(0, stream->codecpar->trailing_padding);
    readAppleGapless(stream->metadata, &m_delay, &m_padding);
    readAppleGapless(m_impl->format->metadata, &m_delay, &m_padding);
    readSideReplayGain(stream, &m_gain);
    readReplayGain(stream->metadata, &m_gain);
    readReplayGain(m_impl->format->metadata, &m_gain);
    return true;
}

void Decoder::setRange(qint64 startFrame, qint64 endFrame)
{
    m_rangeStart = std::max<qint64>(0, startFrame);
    m_rangeEnd = endFrame;
    if (m_impl) {
        m_impl->overflow.clear();
        m_impl->rangeHold = false;
        seekAudibleFrame(m_rangeStart);
    }
}

void Decoder::extendRange(qint64 endFrame)
{
    m_rangeEnd = endFrame;
    if (!m_impl) {
        return;
    }
    if (!m_impl->overflow.empty()) {
        m_impl->pending.insert(m_impl->pending.end(), m_impl->overflow.begin(), m_impl->overflow.end());
        m_impl->overflow.clear();
    }
    if (m_impl->rangeHold) {
        m_impl->rangeHold = false;
        m_impl->codecEof = false;
    }
}

quint64 Decoder::channelMask() const
{
    if (!m_impl || !m_impl->codec || m_impl->codec->ch_layout.order != AV_CHANNEL_ORDER_NATIVE) return 0;
    return m_impl->codec->ch_layout.u.mask;
}

qint64 Decoder::durationMs() const
{
    if (!m_impl || m_format.sampleRate <= 0) {
        return 0;
    }
    qint64 frames = 0;
    if (m_impl->totalRaw > 0) {
        frames = std::max<qint64>(0, m_impl->totalRaw - m_delay - m_padding);
    }
    if (m_rangeEnd >= 0) {
        const qint64 ranged = m_rangeEnd - m_rangeStart;
        frames = frames > 0 ? std::min(frames, ranged) : ranged;
    } else if (m_rangeStart > 0 && frames > 0) {
        frames = std::max<qint64>(0, frames - m_rangeStart);
    }
    return frames > 0 ? (frames * 1000) / m_format.sampleRate : 0;
}

bool Decoder::seekAudibleFrame(qint64 frame)
{
    if (!m_impl) {
        return false;
    }
    const qint64 raw = std::max<qint64>(0, frame) + m_delay;
    const int64_t timestamp = av_rescale_q(raw, AVRational {1, m_format.sampleRate}, m_impl->timeBase);
    const bool sought = avformat_seek_file(m_impl->format, m_impl->streamIndex, std::numeric_limits<int64_t>::min(), timestamp,
                                            timestamp, 0)
            >= 0
        || av_seek_frame(m_impl->format, m_impl->streamIndex, timestamp, AVSEEK_FLAG_BACKWARD) >= 0;
    if (!sought) {
        return false;
    }
    avcodec_flush_buffers(m_impl->codec);
    m_impl->pending.clear();
    m_impl->overflow.clear();
    m_impl->rangeHold = false;
    m_impl->decodeErrors = 0;
    m_impl->codecEof = false;
    m_impl->sawFramePts = false;
    m_impl->nextRaw = raw;
    m_rangeStart = std::max<qint64>(0, frame);
    return true;
}

int Decoder::copyAudible(uint8_t* dst, int maxFrames)
{
    const int frameBytes = m_format.bytesPerFrame();
    const qint64 withhold = m_impl->totalRaw > 0 ? 0 : m_padding;
    const auto withheldBytes = static_cast<size_t>(withhold * frameBytes);
    if (m_impl->pending.size() <= withheldBytes)
        return 0;
    const int available = static_cast<int>((m_impl->pending.size() - withheldBytes) / static_cast<size_t>(frameBytes));
    const int take = std::min(maxFrames, available);
    if (take <= 0)
        return 0;
    const auto bytes = static_cast<size_t>(take) * static_cast<size_t>(frameBytes);
    std::memcpy(dst, m_impl->pending.data(), bytes);
    m_impl->pending.erase(m_impl->pending.begin(), m_impl->pending.begin() + static_cast<std::ptrdiff_t>(bytes));
    return take;
}

int Decoder::decodeAhead()
{
    const int frameBytes = m_format.bytesPerFrame();
    int result = av_read_frame(m_impl->format, m_impl->packet);
        if (result == AVERROR_EOF) {
            avcodec_send_packet(m_impl->codec, nullptr);
        } else if (result < 0) {
            if (result == AVERROR_INVALIDDATA && m_impl->decodeErrors < 8) {
                ++m_impl->decodeErrors;
                return 0;
            }
            return -1;
        } else if (m_impl->packet->stream_index != m_impl->streamIndex) {
            av_packet_unref(m_impl->packet);
            return 0;
        } else {
            result = avcodec_send_packet(m_impl->codec, m_impl->packet);
            av_packet_unref(m_impl->packet);
            if (result < 0 && result != AVERROR(EAGAIN)) {
                if (result == AVERROR_INVALIDDATA && m_impl->decodeErrors < 8) {
                    ++m_impl->decodeErrors;
                    return 0;
                }
                return -1;
            }
        }

        while (true) {
            result = avcodec_receive_frame(m_impl->codec, m_impl->frame);
            if (result == AVERROR(EAGAIN)) {
                break;
            }
            if (result == AVERROR_EOF) {
                m_impl->codecEof = true;
                break;
            }
            if (result < 0) {
                if (result == AVERROR_INVALIDDATA && m_impl->decodeErrors < 8) {
                    ++m_impl->decodeErrors;
                    av_frame_unref(m_impl->frame);
                    break;
                }
                return -1;
            }
            m_impl->decodeErrors = 0;
            AVFrame* frame = m_impl->frame;
            const int samples = frame->nb_samples;
            std::vector<uint8_t> interleaved(static_cast<size_t>(samples) * static_cast<size_t>(frameBytes));
            if (m_impl->resampler) {
                uint8_t* output[] = {interleaved.data()};
                const int converted = swr_convert(m_impl->resampler, output, samples,
                                                   const_cast<const uint8_t**>(frame->extended_data), samples);
                if (converted < 0) {
                    av_frame_unref(frame);
                    return -1;
                }
            } else if (frame->data[0]) {
                std::memcpy(interleaved.data(), frame->data[0], interleaved.size());
            }

            qint64 frameRaw = m_impl->nextRaw;
            if (frame->pts != AV_NOPTS_VALUE) {
                frameRaw = av_rescale_q(frame->pts, m_impl->timeBase, AVRational {1, m_format.sampleRate});
                m_impl->sawFramePts = true;
            }
            int sideSkip = 0;
            if (const auto* side = av_frame_get_side_data(frame, AV_FRAME_DATA_SKIP_SAMPLES); side && side->size >= 10 && m_delay <= 0) {
                sideSkip = static_cast<int>(AV_RL32(side->data));
                if (m_padding <= 0) {
                    m_padding = AV_RL32(side->data + 4);
                }
            }
            auto audibleAt = [&](int index) {
                const qint64 raw = frameRaw + index;
                if (raw < m_delay || index < sideSkip) {
                    return false;
                }
                if (m_impl->totalRaw > 0 && m_padding > 0 && raw >= m_impl->totalRaw - m_padding) {
                    return false;
                }
                const qint64 audible = raw - m_delay;
                if (audible < m_rangeStart) {
                    return false;
                }
                if (m_rangeEnd >= 0 && audible >= m_rangeEnd) {
                    return false;
                }
                return true;
            };
            int overflowStart = -1;
            int index = 0;
            while (index < samples && !m_impl->codecEof) {
                while (index < samples && !audibleAt(index)) {
                    ++index;
                }
                const int start = index;
                while (index < samples && !m_impl->codecEof && audibleAt(index)) {
                    ++index;
                }
                if (start < index) {
                    const auto offset = static_cast<size_t>(start) * static_cast<size_t>(frameBytes);
                    const auto bytes = static_cast<size_t>(index - start) * static_cast<size_t>(frameBytes);
                    m_impl->pending.insert(m_impl->pending.end(), interleaved.begin() + static_cast<std::ptrdiff_t>(offset),
                                           interleaved.begin() + static_cast<std::ptrdiff_t>(offset + bytes));
                }
            }
            for (int sample = 0; sample < samples; ++sample) {
                const qint64 audible = frameRaw + sample - m_delay;
                if (m_rangeEnd >= 0 && audible >= m_rangeEnd) {
                    overflowStart = sample;
                    break;
                }
            }
            if (overflowStart >= 0) {
                const auto offset = static_cast<size_t>(overflowStart) * static_cast<size_t>(frameBytes);
                m_impl->overflow.insert(m_impl->overflow.end(), interleaved.begin() + static_cast<std::ptrdiff_t>(offset), interleaved.end());
                m_impl->rangeHold = true;
                m_impl->codecEof = true;
            }
            m_impl->nextRaw = frameRaw + samples;
            av_frame_unref(frame);
            if (m_impl->codecEof) {
                break;
            }
        }
    return 0;
}

int Decoder::read(uint8_t* dst, int maxFrames)
{
    if (!m_impl || maxFrames <= 0)
        return 0;
    while (true) {
        if (const int ready = copyAudible(dst, maxFrames); ready > 0)
            return ready;
        if (m_impl->codecEof)
            return 0;
        if (const int decoded = decodeAhead(); decoded < 0)
            return decoded;
    }
}

} // namespace echo
