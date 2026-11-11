// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/audio/midi.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace das::audio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kGain = 0.25f; // level of one voice at full volume

// --- reading -----------------------------------------------------------------

class Reader {
public:
    Reader(const data::Bytes& bytes, std::size_t begin, std::size_t end) : bytes_(bytes), at_(begin), end_(end) {}

    bool done() const { return at_ >= end_; }
    std::size_t at() const { return at_; }
    std::uint8_t byte() {
        if (at_ >= end_) {
            throw data::DataError("MIDI data ends early");
        }
        return bytes_[at_++];
    }
    std::uint32_t big(int count) {
        std::uint32_t v = 0;
        for (int i = 0; i < count; ++i) {
            v = (v << 8) | byte();
        }
        return v;
    }
    std::uint32_t varLength() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const std::uint8_t b = byte();
            v = (v << 7) | (b & 0x7fu);
            if ((b & 0x80u) == 0) {
                return v;
            }
        }
        throw data::DataError("MIDI length too long");
    }
    void skip(std::uint32_t count) {
        if (count > end_ - at_) {
            throw data::DataError("MIDI data ends early");
        }
        at_ += count;
    }

private:
    const data::Bytes& bytes_;
    std::size_t at_;
    std::size_t end_;
};

struct TickEvent {
    std::uint64_t tick;
    std::size_t order; // keeps file order for events at the same tick
    MidiEvent event;
};

// --- sounds --------------------------------------------------------------------

// One sound per General MIDI instrument family (program / 8).
struct Patch {
    float ratio;    // modulator frequency / carrier frequency
    float index;    // modulation depth
    float attack;   // seconds
    float decay;    // seconds (time constant towards the sustain level)
    float sustain;  // level, 0 to 1
    float release;  // seconds (time constant)
    float modDecay; // seconds; 0 keeps the modulation depth
};

constexpr Patch kPatches[16] = {
    {1.0f, 1.8f, 0.002f, 0.9f, 0.0f, 0.25f, 0.6f},  // piano
    {3.5f, 2.5f, 0.001f, 1.2f, 0.0f, 0.5f, 0.4f},   // chromatic percussion
    {1.0f, 0.8f, 0.01f, 0.1f, 0.9f, 0.08f, 0.0f},   // organ
    {1.0f, 2.2f, 0.002f, 0.7f, 0.05f, 0.2f, 0.3f},  // guitar
    {1.0f, 1.5f, 0.003f, 0.5f, 0.4f, 0.1f, 0.3f},   // bass
    {1.0f, 1.1f, 0.12f, 0.3f, 0.85f, 0.35f, 0.0f},  // strings
    {1.0f, 1.0f, 0.15f, 0.3f, 0.85f, 0.4f, 0.0f},   // ensemble
    {1.0f, 2.5f, 0.05f, 0.2f, 0.8f, 0.15f, 0.0f},   // brass
    {2.0f, 1.2f, 0.03f, 0.2f, 0.8f, 0.1f, 0.0f},    // reed
    {1.0f, 0.4f, 0.06f, 0.2f, 0.8f, 0.12f, 0.0f},   // pipe
    {1.0f, 3.0f, 0.005f, 0.2f, 0.8f, 0.1f, 0.0f},   // synth lead
    {1.0f, 1.0f, 0.4f, 0.5f, 0.8f, 0.8f, 0.0f},     // synth pad
    {1.41f, 2.0f, 0.2f, 0.5f, 0.6f, 0.6f, 0.0f},    // synth effects
    {3.0f, 1.5f, 0.002f, 0.6f, 0.0f, 0.2f, 0.3f},   // ethnic
    {1.6f, 3.0f, 0.001f, 0.4f, 0.0f, 0.2f, 0.2f},   // percussive
    {1.7f, 4.0f, 0.01f, 0.5f, 0.3f, 0.3f, 0.0f},    // sound effects
};

enum Drum { Kick, Snare, ClosedHat, OpenHat, Tom, Cymbal, OtherDrum };

int drumFor(int note) {
    switch (note) {
    case 35:
    case 36: return Kick;
    case 37:
    case 38:
    case 40: return Snare;
    case 42:
    case 44: return ClosedHat;
    case 46: return OpenHat;
    case 41:
    case 43:
    case 45:
    case 47:
    case 48:
    case 50: return Tom;
    case 49:
    case 51:
    case 52:
    case 53:
    case 55:
    case 57:
    case 59: return Cymbal;
    default: return OtherDrum;
    }
}

float drumDecay(int drum) {
    switch (drum) {
    case Kick: return 0.25f;
    case Snare: return 0.18f;
    case ClosedHat: return 0.05f;
    case OpenHat: return 0.3f;
    case Tom: return 0.3f;
    case Cymbal: return 0.9f;
    default: return 0.12f;
    }
}

double noteFrequency(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

// Volume, expression and velocity work on a curve, like on General MIDI
// synthesizers: half the value is about a quarter of the loudness.
float curve(float value) { return value * value; }

// Families that are played with a bow or breath get a slow vibrato after
// a moment, so long notes do not sound like test tones.
bool hasVibrato(int family) { return family == 5 || family == 6 || family == 7 || family == 8 || family == 9; }

// Leaves quiet and normal levels alone and bends only the loudest peaks
// below full scale, so chords never crackle.
float limit(float x) {
    constexpr float knee = 0.6f;
    const float size = std::abs(x);
    if (size <= knee) {
        return x;
    }
    const float over = knee + (1.0f - knee) * std::tanh((size - knee) / (1.0f - knee));
    return x < 0 ? -over : over;
}

} // namespace

bool isMidi(const data::Bytes& bytes) {
    return bytes.size() >= 14 && bytes[0] == 'M' && bytes[1] == 'T' && bytes[2] == 'h' && bytes[3] == 'd';
}

MidiSong parseMidi(const data::Bytes& bytes) {
    if (!isMidi(bytes)) {
        throw data::DataError("not a MIDI file");
    }
    Reader header(bytes, 0, bytes.size());
    header.skip(4);
    const std::uint32_t headerLength = header.big(4);
    if (headerLength < 6) {
        throw data::DataError("MIDI header too short");
    }
    const std::uint32_t format = header.big(2);
    const std::uint32_t trackCount = header.big(2);
    const std::uint32_t division = header.big(2);
    if (format > 1) {
        throw data::DataError("MIDI format " + std::to_string(format) + " is not supported");
    }
    if (division == 0 || (division & 0x8000u) != 0) {
        throw data::DataError("MIDI timing in frames is not supported");
    }
    header.skip(headerLength - 6);

    std::vector<TickEvent> events;
    std::map<std::uint64_t, std::uint32_t> tempos; // tick -> microseconds per quarter note
    std::size_t order = 0;
    std::size_t at = header.at();
    for (std::uint32_t t = 0; t < trackCount && at + 8 <= bytes.size(); ++t) {
        Reader chunk(bytes, at, bytes.size());
        const std::uint32_t id = chunk.big(4);
        const std::uint32_t length = chunk.big(4);
        const std::size_t begin = chunk.at();
        if (length > bytes.size() - begin) {
            throw data::DataError("MIDI track ends early");
        }
        at = begin + length;
        if (id != 0x4d54726bu) { // "MTrk"; other chunks are skipped
            continue;
        }
        Reader r(bytes, begin, begin + length);
        std::uint64_t tick = 0;
        std::uint8_t running = 0;
        while (!r.done()) {
            tick += r.varLength();
            std::uint8_t status = r.byte();
            if (status == 0xff) { // meta event
                const std::uint8_t type = r.byte();
                const std::uint32_t size = r.varLength();
                if (type == 0x51 && size == 3) {
                    tempos[tick] = r.big(3);
                } else if (type == 0x2f) {
                    r.skip(size);
                    break;
                } else {
                    r.skip(size);
                }
                continue;
            }
            if (status == 0xf0 || status == 0xf7) { // system exclusive
                r.skip(r.varLength());
                continue;
            }
            std::uint8_t first = 0;
            if (status < 0x80) { // running status: this was the first data byte
                if (running == 0) {
                    throw data::DataError("MIDI data byte without a status");
                }
                first = status;
                status = running;
            } else {
                running = status;
                first = r.byte();
            }
            const std::uint8_t kind = status & 0xf0u;
            const std::uint8_t second = (kind == 0xc0 || kind == 0xd0) ? 0 : r.byte();
            events.push_back({tick, order++, {0, status, static_cast<std::uint8_t>(first & 0x7fu),
                                              static_cast<std::uint8_t>(second & 0x7fu)}});
        }
    }
    // Songs made for Windows 3.1 and 95 hold the music twice: for a General
    // MIDI synthesizer on channels 1 to 10, and a simpler copy for an FM card
    // on channels 13 to 16, with its drums on 16. Windows played only one of
    // the two, so when a file has both we keep the General MIDI part.
    bool full = false;
    bool simple = false;
    for (const auto& e : events) {
        if ((e.event.status & 0xf0u) == 0x90 && e.event.data2 > 0) {
            const int channel = e.event.status & 0x0f;
            full = full || channel <= 9;
            simple = simple || channel >= 12;
        }
    }
    if (full && simple) {
        std::erase_if(events, [](const TickEvent& e) { return (e.event.status & 0x0f) >= 12; });
    }

    std::sort(events.begin(), events.end(), [](const TickEvent& a, const TickEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });

    // Ticks to seconds, through the tempo changes.
    MidiSong song;
    std::uint64_t lastTick = 0;
    double lastTime = 0;
    double secondsPerTick = 0.5 / division; // 120 beats per minute until told otherwise
    auto tempo = tempos.begin();
    for (const auto& e : events) {
        while (tempo != tempos.end() && tempo->first <= e.tick) {
            lastTime += static_cast<double>(tempo->first - lastTick) * secondsPerTick;
            lastTick = tempo->first;
            secondsPerTick = tempo->second / 1e6 / division;
            ++tempo;
        }
        MidiEvent out = e.event;
        out.time = lastTime + static_cast<double>(e.tick - lastTick) * secondsPerTick;
        song.events.push_back(out);
    }
    song.length = song.events.empty() ? 0 : song.events.back().time;
    return song;
}

Synth::Synth(int sampleRate) : rate_(sampleRate) {}

void Synth::play(MidiSong song) {
    song_ = std::move(song);
    next_ = 0;
    time_ = 0;
    channels_ = {};
    for (auto& v : voices_) {
        v.active = false;
    }
    playing_ = true;
}

void Synth::stop() {
    playing_ = false;
    for (auto& v : voices_) {
        v.active = false;
    }
}

void Synth::apply(const MidiEvent& e) {
    const int channel = e.status & 0x0f;
    Channel& c = channels_[static_cast<std::size_t>(channel)];
    switch (e.status & 0xf0) {
    case 0x80: noteOff(channel, e.data1); break;
    case 0x90:
        if (e.data2 == 0) {
            noteOff(channel, e.data1);
        } else {
            noteOn(channel, e.data1, e.data2);
        }
        break;
    case 0xb0:
        switch (e.data1) {
        case 7: c.volume = e.data2 / 127.0f; break;
        case 10: c.pan = e.data2 / 127.0f; break;
        case 11: c.expression = e.data2 / 127.0f; break;
        case 64:
            c.sustain = e.data2 >= 64;
            if (!c.sustain) {
                for (auto& v : voices_) {
                    if (v.active && v.channel == channel && !v.held) {
                        v.stage = 2;
                    }
                }
            }
            break;
        case 120:
        case 123:
            for (auto& v : voices_) {
                if (v.active && v.channel == channel) {
                    v.held = false;
                    v.stage = 2;
                }
            }
            break;
        case 121:
            c = Channel{.program = c.program};
            break;
        default: break;
        }
        break;
    case 0xc0: c.program = e.data1; break;
    case 0xe0: {
        const int value = (e.data2 << 7) | e.data1;
        c.bend = static_cast<float>(value - 8192) / 8192.0f * 2.0f;
        for (auto& v : voices_) {
            if (v.active && v.channel == channel && v.drum < 0) {
                v.frequency = noteFrequency(v.note + static_cast<double>(c.bend));
            }
        }
        break;
    }
    default: break;
    }
}

void Synth::noteOn(int channel, int note, int velocity) {
    // The same key struck again lets go of the note it was still playing.
    for (auto& v : voices_) {
        if (v.active && v.channel == channel && v.note == note && v.drum < 0 && v.stage != 2) {
            v.held = false;
            v.stage = 2;
        }
    }
    // A free voice, or else the quietest one.
    Voice* voice = nullptr;
    for (auto& v : voices_) {
        if (!v.active) {
            voice = &v;
            break;
        }
    }
    if (!voice) {
        voice = &*std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
            const float qa = a.stage == 2 ? a.env * 0.5f : a.env;
            const float qb = b.stage == 2 ? b.env * 0.5f : b.env;
            return qa < qb;
        });
    }
    const std::uint32_t seed = voice->noise == 0 ? 1u : voice->noise;
    *voice = Voice{};
    voice->noise = seed;
    voice->active = true;
    voice->channel = channel;
    voice->note = note;
    voice->velocity = curve(static_cast<float>(velocity) / 127.0f);
    voice->drum = channel == 9 ? drumFor(note) : -1;
    voice->frequency = noteFrequency(note + static_cast<double>(channels_[static_cast<std::size_t>(channel)].bend));
    if (voice->drum >= 0) {
        voice->env = 1;
        voice->stage = 1;
    }
}

void Synth::noteOff(int channel, int note) {
    const bool sustain = channels_[static_cast<std::size_t>(channel)].sustain;
    for (auto& v : voices_) {
        if (v.active && v.channel == channel && v.note == note && v.held) {
            v.held = false;
            if (!sustain && v.drum < 0) {
                v.stage = 2;
            }
        }
    }
}

float Synth::voiceSample(Voice& v) {
    const double dt = 1.0 / rate_;
    v.age += dt;
    auto noise = [&v]() {
        v.noise ^= v.noise << 13;
        v.noise ^= v.noise >> 17;
        v.noise ^= v.noise << 5;
        return static_cast<float>(v.noise) / 2147483648.0f - 1.0f;
    };
    if (v.drum >= 0) {
        v.env *= static_cast<float>(std::exp(-dt / drumDecay(v.drum)));
        if (v.env < 0.001f) {
            v.active = false;
            return 0;
        }
        float s = 0;
        switch (v.drum) {
        case Kick:
        case Tom: {
            const double base = v.drum == Kick ? 45.0 : 70.0 + (v.note - 41) * 12.0;
            const double f = base + base * 1.8 * std::exp(-v.age / 0.04);
            v.carrier += f * dt;
            s = static_cast<float>(std::sin(2 * kPi * v.carrier));
            break;
        }
        case Snare:
            v.carrier += 190.0 * dt;
            s = 0.6f * noise() + 0.4f * static_cast<float>(std::sin(2 * kPi * v.carrier));
            break;
        case ClosedHat:
        case OpenHat:
        case Cymbal: {
            // Noise with its low part taken out: the difference of two samples.
            const float n = noise();
            s = (n - static_cast<float>(v.modulator)) * 0.5f;
            v.modulator = n;
            s *= v.drum == Cymbal ? 0.6f : 0.8f;
            break;
        }
        default: s = noise() * 0.7f; break;
        }
        return s * v.env * v.velocity;
    }
    const int family = channels_[static_cast<std::size_t>(v.channel)].program / 8 % 16;
    const Patch& p = kPatches[family];
    switch (v.stage) {
    case 0:
        v.env += static_cast<float>(dt / std::max(0.001f, p.attack));
        if (v.env >= 1) {
            v.env = 1;
            v.stage = 1;
        }
        break;
    case 1: v.env = p.sustain + (v.env - p.sustain) * static_cast<float>(std::exp(-dt / p.decay)); break;
    default: v.env *= static_cast<float>(std::exp(-dt / p.release)); break;
    }
    if (p.modDecay > 0) {
        v.modEnv = std::max(0.2f, v.modEnv * static_cast<float>(std::exp(-dt / p.modDecay)));
    }
    if (v.env < 0.0005f && (v.stage == 2 || (v.stage == 1 && p.sustain <= 0))) {
        v.active = false;
        return 0;
    }
    double frequency = v.frequency;
    if (hasVibrato(family) && v.age > 0.25) {
        const double depth = std::min(1.0, (v.age - 0.25) / 0.4) * 0.004; // about 1/14 semitone
        frequency *= 1.0 + depth * std::sin(2 * kPi * 5.5 * v.age);
    }
    v.modulator += frequency * p.ratio * dt;
    v.carrier += frequency * dt;
    v.modulator -= std::floor(v.modulator);
    v.carrier -= std::floor(v.carrier);
    // Softer notes are a little darker, as on real instruments.
    const double brightness = 0.55 + 0.45 * std::sqrt(v.velocity);
    const double mod = std::sin(2 * kPi * v.modulator) * p.index * v.modEnv * brightness;
    return static_cast<float>(std::sin(2 * kPi * v.carrier + mod)) * v.env * v.velocity;
}

void Synth::render(float* out, int frames) {
    if (!playing_) {
        return;
    }
    const double dt = 1.0 / rate_;
    for (int f = 0; f < frames; ++f) {
        while (next_ < song_.events.size() && song_.events[next_].time <= time_) {
            apply(song_.events[next_++]);
        }
        float left = 0;
        float right = 0;
        bool any = false;
        for (auto& v : voices_) {
            if (!v.active) {
                continue;
            }
            any = true;
            const Channel& c = channels_[static_cast<std::size_t>(v.channel)];
            const float s = voiceSample(v) * curve(c.volume) * curve(c.expression) * kGain;
            left += s * std::sqrt(1.0f - c.pan);
            right += s * std::sqrt(c.pan);
        }
        // A soft limit on the peaks instead of hard clipping.
        out[2 * f] += limit(left);
        out[2 * f + 1] += limit(right);
        time_ += dt;
        if (!any && next_ >= song_.events.size()) {
            playing_ = false;
            return;
        }
    }
}

} // namespace das::audio
