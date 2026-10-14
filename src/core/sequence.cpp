#include "core/sequence.h"

#include <algorithm>

namespace sharkcut {

SequenceLayout makeLayout(ClipRange a, ClipRange b, std::int64_t transitionFrames) {
    SequenceLayout layout;
    layout.a = a;
    layout.b = b;
    const std::int64_t limit = std::min(a.length(), b.length());
    layout.transitionFrames = limit > 0 ? std::clamp<std::int64_t>(transitionFrames, 1, limit) : 0;
    return layout;
}

SequenceLayout::At SequenceLayout::at(std::int64_t frame) const {
    At result;
    const std::int64_t total = length();
    if (total <= 0) {
        return result;
    }
    frame = std::clamp<std::int64_t>(frame, 0, total - 1);

    const std::int64_t start = transitionStart();
    if (frame < a.length()) {
        result.aFrame = a.in + frame;
    }
    if (frame >= start) {
        result.bFrame = b.in + (frame - start);
    }
    if (frame >= start && frame < transitionEnd()) {
        result.transitionFrame = frame - start;
    }
    return result;
}

} // namespace sharkcut
