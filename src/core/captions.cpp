#include "core/captions.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <limits>
#include <optional>

namespace sharkcut {

namespace {

std::int64_t usToFrame(std::int64_t us, FrameRate rate) {
    return rescale(us, kMicroseconds, rate.frameDuration(), Rounding::Nearest);
}

std::int64_t frameToUs(std::int64_t frame, FrameRate rate) {
    return rescale(frame, rate.frameDuration(), kMicroseconds, Rounding::Nearest);
}

std::string trim(std::string_view text) {
    std::size_t a = 0;
    std::size_t b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) {
        --b;
    }
    return std::string(text.substr(a, b - a));
}

std::vector<std::string> splitWords(std::string_view text) {
    std::vector<std::string> words;
    std::string current;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                words.push_back(std::move(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        words.push_back(std::move(current));
    }
    return words;
}

bool endsSentence(std::string_view word) {
    for (const std::string_view end : {".", "?", "!", "…"}) {
        if (word.ends_with(end)) {
            return true;
        }
    }
    return false;
}

// Subtítulo en construcción: sus palabras y sus líneas.
struct Group {
    std::vector<TimedWord> words;
    std::vector<std::string> lines;
};

} // namespace

std::size_t utf8Length(std::string_view text) {
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));
}

std::string withoutPunctuation(std::string_view text) {
    static constexpr std::array<std::string_view, 16> kMarks = {
        ".", ",", ";", ":", "!", "?", "¡", "¿", "…", "\"", "«", "»", "“", "”", "(", ")"};
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        bool skipped = false;
        for (const std::string_view mark : kMarks) {
            if (text.substr(i).starts_with(mark)) {
                i += mark.size();
                skipped = true;
                break;
            }
        }
        if (!skipped) {
            result += text[i++];
        }
    }
    return result;
}

std::vector<Caption> layoutCaptions(const std::vector<TimedWord>& input,
                                    const CaptionLayout& layout, FrameRate rate) {
    const std::size_t maxLines = layout.lines == CaptionLayout::Lines::Double ? 2 : 1;
    const std::size_t maxChars = static_cast<std::size_t>(std::max(layout.maxChars, 1));

    std::vector<Group> groups;
    Group current;
    const auto close = [&] {
        if (!current.words.empty()) {
            groups.push_back(std::move(current));
        }
        current = {};
    };
    for (const TimedWord& original : input) {
        TimedWord word = original;
        word.text = trim(layout.removePunctuation ? withoutPunctuation(word.text) : word.text);
        if (word.text.empty()) {
            continue;
        }
        word.endUs = std::max(word.endUs, word.startUs);
        if (!current.words.empty() &&
            (layout.lines == CaptionLayout::Lines::Word ||
             word.startUs - current.words.back().endUs > kCaptionPauseUs)) {
            close();
        }
        const std::size_t length = utf8Length(word.text);
        if (current.lines.empty()) {
            current.lines.push_back(word.text);
        } else if (utf8Length(current.lines.back()) + 1 + length <= maxChars) {
            current.lines.back() += ' ' + word.text;
        } else if (current.lines.size() < maxLines) {
            current.lines.push_back(word.text);
        } else {
            close();
            current.lines.push_back(word.text);
        }
        current.words.push_back(word);
        if (endsSentence(original.text)) {
            close();
        }
    }
    close();

    const std::int64_t minFrames = std::max<std::int64_t>(1, usToFrame(layout.minDurationUs, rate));
    const std::int64_t gap = std::max<std::int64_t>(0, layout.gapFrames);
    std::vector<Caption> captions;
    std::int64_t previousEnd = std::numeric_limits<std::int64_t>::min();
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const Group& g = groups[i];
        std::int64_t start = std::max<std::int64_t>(0, usToFrame(g.words.front().startUs, rate));
        if (!captions.empty()) {
            start = std::max(start, previousEnd + gap);
        }
        std::int64_t end = std::max(usToFrame(g.words.back().endUs, rate), start + minFrames);
        if (i + 1 < groups.size()) {
            // Sin pisar el siguiente (la duración mínima cede ante él).
            const std::int64_t next = usToFrame(groups[i + 1].words.front().startUs, rate) - gap;
            end = std::min(end, std::max(next, usToFrame(g.words.back().endUs, rate)));
        }
        end = std::max(end, start + 1);

        Caption caption;
        caption.start = start;
        caption.length = end - start;
        for (std::size_t l = 0; l < g.lines.size(); ++l) {
            caption.text += (l > 0 ? "\n" : "") + g.lines[l];
        }
        for (const TimedWord& w : g.words) {
            const std::int64_t ws =
                std::clamp(usToFrame(w.startUs, rate) - start, std::int64_t{0}, caption.length - 1);
            const std::int64_t we =
                std::clamp(usToFrame(w.endUs, rate) - start, ws + 1, caption.length);
            caption.words.push_back({ws, we, w.text});
        }
        previousEnd = end;
        captions.push_back(std::move(caption));
    }
    return captions;
}

std::vector<TimedWord> captionWords(const CaptionTrack& track, FrameRate rate) {
    std::vector<TimedWord> result;
    for (const Caption& c : track.captions) {
        if (!c.words.empty()) {
            for (const CaptionWord& w : c.words) {
                result.push_back(
                    {frameToUs(c.start + w.start, rate), frameToUs(c.start + w.end, rate), w.text});
            }
            continue;
        }
        const std::vector<std::string> words = splitWords(c.text);
        std::size_t total = 0;
        for (const std::string& w : words) {
            total += utf8Length(w) + 1;
        }
        const std::int64_t startUs = frameToUs(c.start, rate);
        const std::int64_t lengthUs = frameToUs(c.end(), rate) - startUs;
        std::size_t done = 0;
        for (const std::string& w : words) {
            const std::size_t size = utf8Length(w) + 1;
            const std::int64_t a = startUs + mulDiv(lengthUs, static_cast<std::int64_t>(done),
                                                    static_cast<std::int64_t>(total));
            done += size;
            const std::int64_t b = startUs + mulDiv(lengthUs, static_cast<std::int64_t>(done),
                                                    static_cast<std::int64_t>(total));
            result.push_back({a, b, w});
        }
    }
    return result;
}

// --- .srt ---------------------------------------------------------------------------

namespace {

std::string srtTime(std::int64_t us) {
    us = std::max<std::int64_t>(0, us);
    const std::int64_t ms = (us + 500) / 1000;
    char text[32];
    std::snprintf(text, sizeof text, "%02lld:%02lld:%02lld,%03lld",
                  static_cast<long long>(ms / 3'600'000), static_cast<long long>(ms / 60'000 % 60),
                  static_cast<long long>(ms / 1000 % 60), static_cast<long long>(ms % 1000));
    return text;
}

// "HH:MM:SS,mmm" (o con punto) → microsegundos.
std::optional<std::int64_t> parseSrtTime(std::string_view text) {
    long long h = 0, m = 0, s = 0, ms = 0;
    char sep = 0;
    const std::string copy(trim(text));
    if (std::sscanf(copy.c_str(), "%lld:%lld:%lld%c%lld", &h, &m, &s, &sep, &ms) != 5 ||
        (sep != ',' && sep != '.')) {
        return std::nullopt;
    }
    return ((h * 3600 + m * 60 + s) * 1000 + ms) * 1000;
}

} // namespace

std::string captionsToSrt(const CaptionTrack& track, FrameRate rate) {
    std::string out;
    int n = 0;
    for (const Caption& c : track.captions) {
        out += std::to_string(++n) + "\n";
        out +=
            srtTime(frameToUs(c.start, rate)) + " --> " + srtTime(frameToUs(c.end(), rate)) + "\n";
        out += c.text + "\n\n";
    }
    return out;
}

std::vector<SrtEntry> parseSrt(std::string_view text) {
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.remove_prefix(3);
    }
    // Líneas sin '\r'.
    std::vector<std::string> lines;
    std::string line;
    for (const char c : text) {
        if (c == '\n') {
            lines.push_back(std::move(line));
            line.clear();
        } else if (c != '\r') {
            line += c;
        }
    }
    lines.push_back(std::move(line));

    std::vector<SrtEntry> entries;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::size_t arrow = lines[i].find("-->");
        if (arrow == std::string::npos) {
            continue;
        }
        const auto a = parseSrtTime(std::string_view(lines[i]).substr(0, arrow));
        const auto b = parseSrtTime(std::string_view(lines[i]).substr(arrow + 3));
        std::string body;
        std::size_t j = i + 1;
        for (; j < lines.size() && !trim(lines[j]).empty(); ++j) {
            body += (body.empty() ? "" : "\n") + trim(lines[j]);
        }
        i = j;
        if (a && b && *b > *a && !body.empty()) {
            entries.push_back({*a, *b, std::move(body)});
        }
    }
    return entries;
}

std::vector<Caption> captionsFromSrt(const std::vector<SrtEntry>& input, FrameRate rate) {
    std::vector<SrtEntry> entries = input;
    std::stable_sort(entries.begin(), entries.end(),
                     [](const SrtEntry& a, const SrtEntry& b) { return a.startUs < b.startUs; });
    std::vector<Caption> captions;
    for (const SrtEntry& e : entries) {
        Caption c;
        c.start = std::max<std::int64_t>(0, usToFrame(e.startUs, rate));
        if (!captions.empty()) {
            c.start = std::max(c.start, captions.back().end());
        }
        c.length = std::max<std::int64_t>(1, usToFrame(e.endUs, rate) - c.start);
        c.text = e.text;
        captions.push_back(std::move(c));
    }
    return captions;
}

} // namespace sharkcut
