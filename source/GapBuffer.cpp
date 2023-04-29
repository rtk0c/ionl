#include "GapBuffer.hpp"

#include "imgui_internal.h"

#include <cstdlib>
#include <utility>

static ImWchar* AllocateBuffer(size_t size) {
    return (ImWchar*)malloc(sizeof(ImWchar) * size);
}

static void ReallocateBuffer(ImWchar*& oldBuffer, size_t newSize) {
    oldBuffer = (ImWchar*)realloc(oldBuffer, sizeof(ImWchar) * newSize);
}

static void DeallocateBuffer(ImWchar* buffer) {
    free(buffer);
}

Ionl::GapBuffer::GapBuffer()
    : buffer{ AllocateBuffer(256) }
    , bufferSize{ 256 }
    , frontSize{ 0 }
    , gapSize{ 256 } {}

Ionl::GapBuffer::GapBuffer(std::string_view content)
    // NOTE: these set of parameters are technically invalid, but they get immediately overridden by UpdateContent() which doesn't care
    : buffer{ nullptr }
    , bufferSize{ 0 }
    , frontSize{ 0 }
    , gapSize{ 0 } //
{
    UpdateContent(content);
}

Ionl::GapBuffer::GapBuffer(GapBuffer&& that) noexcept
    : buffer{ that.buffer }
    , bufferSize{ that.bufferSize }
    , frontSize{ that.frontSize }
    , gapSize{ that.gapSize } //
{
    that.buffer = nullptr;
    that.bufferSize = 0;
    that.frontSize = 0;
    that.gapSize = 0;
}

Ionl::GapBuffer& Ionl::GapBuffer::operator=(GapBuffer&& that) noexcept {
    if (this == &that) {
        return *this;
    }

    DeallocateBuffer(this->buffer);
    this->buffer = std::exchange(that.buffer, nullptr);
    this->bufferSize = std::exchange(that.bufferSize, 0);
    this->frontSize = std::exchange(that.frontSize, 0);
    this->gapSize = std::exchange(that.gapSize, 0);

    return *this;
}

Ionl::GapBuffer::~GapBuffer() {
    DeallocateBuffer(buffer);
}

int64_t Ionl::GapBuffer::GetLastTextIndex() const {
    if (GetBackSize() > 0) return GetBackEnd() - 1;
    if (GetFrontSize() > 0) return GetFrontEnd() - 1;
    return 0;
}

int64_t Ionl::GapBuffer::GetLastTextEnd() const {
    return GetBackSize() > 0 ? GetBackEnd() : GetFrontEnd();
}

std::string Ionl::GapBuffer::ExtractContent() const {
    auto frontBegin = buffer;
    auto frontEnd = buffer + frontSize;
    auto backBegin = buffer + frontSize + gapSize;
    auto backEnd = buffer + bufferSize;

    size_t utf8Count =
        ImTextCountUtf8BytesFromStr(frontBegin, frontEnd) +
        ImTextCountUtf8BytesFromStr(backBegin, backEnd);

    // Add 1 to string buffer size to account for null terminator
    // ImTextStrToUtf8() writes the \0 at the end, in addition to the provided source content
    std::string result(utf8Count, '\0');
    size_t frontUtf8Count = ImTextStrToUtf8(result.data(), result.size() + 1, frontBegin, frontEnd);
    size_t backUtf8Count = ImTextStrToUtf8(result.data() + frontUtf8Count, result.size() - frontUtf8Count + 1, backBegin, backEnd);

    return result;
}

void Ionl::GapBuffer::UpdateContent(std::string_view content) {
    auto strBegin = &*content.begin();
    auto strEnd = &*content.end();
    auto minBufferSize = ImTextCountCharsFromUtf8(strBegin, strEnd);
    if (bufferSize < minBufferSize) {
        bufferSize = minBufferSize;
        frontSize = minBufferSize;
        gapSize = 0;
        ReallocateBuffer(buffer, minBufferSize);
    } else {
        // If new string size is smaller than our current buffer, we keep the buffer and simply put new data into it
        frontSize = minBufferSize;
        gapSize = bufferSize - minBufferSize;
    }
    ImTextStrFromUtf8NoNullTerminate(buffer, bufferSize, strBegin, strEnd);
}

int64_t Ionl::MapLogicalIndexToBufferIndex(const GapBuffer& buffer, int64_t logicalIdx) {
    if (logicalIdx < buffer.frontSize) {
        return logicalIdx;
    } else {
        return logicalIdx + buffer.gapSize;
    }
}

int64_t Ionl::MapBufferIndexToLogicalIndex(const GapBuffer& buffer, int64_t bufferIdx) {
    if (bufferIdx < buffer.frontSize) {
        return bufferIdx;
    } else if (/* bufferIdx >= buffer.frontSize && */ bufferIdx < (buffer.frontSize + buffer.gapSize)) {
        return -1;
    } else {
        return bufferIdx - buffer.gapSize;
    }
}

int64_t Ionl::AdjustBufferIndex(const GapBuffer& buffer, int64_t idx, int64_t delta) {
    int64_t gapBeginIdx = buffer.frontSize;
    int64_t gapEndIdx = buffer.frontSize + buffer.gapSize;
    int64_t gapSize = buffer.gapSize;

    if (idx >= gapEndIdx) {
        return idx + delta < gapEndIdx
            ? idx + (-gapSize) + delta
            : idx + delta;
    } else {
        return idx + delta >= gapBeginIdx
            ? idx + (+gapSize) + delta
            : idx + delta;
    }
}

void Ionl::MoveGapToBufferIndex(Ionl::GapBuffer& buf, int64_t newIdx) {
    int64_t oldIdx = buf.GetGapBegin();
    if (oldIdx == newIdx) return;

    // NOTE: we must use memmove() because gap size may be smaller than movement distance, in which case the src region and dst region will overlap
    if (oldIdx < newIdx) {
        // Moving towards end of buffer

        //        oldIdx
        //          |   .- newIdx
        //          V   V
        //     *****------*********
        //                └──┘|
        //                ┃   ^ gapEnd
        //          ┌──┐🠘┛
        //     *********------*****

        // Clamp newIdx to make sure the new range still fits inside the buffer
        if (buf.bufferSize - newIdx < buf.gapSize) {
            newIdx = buf.bufferSize - buf.gapSize;
        }
        size_t size = newIdx - oldIdx;
        memmove(buf.buffer + oldIdx, buf.buffer + buf.GetGapEnd() - size, size);
    } else /* oldIdx > newIdx */ {
        // Moving towards beginning of buffer

        //        newIdx
        //          |   .- oldIdx
        //          V   V
        //     *********------*****
        //          └──┘━━┓
        //                🠛
        //                ┌──┐
        //     *****------*********
        //                    ^ gapEnd

        size_t size = oldIdx - newIdx;
        memmove(buf.buffer + buf.GetGapEnd() - size, buf.buffer + newIdx, size);
    }
    buf.frontSize = newIdx;
}

void Ionl::MoveGapToLogicalIndex(GapBuffer& buf, int64_t newIdxLogical) {
    // To achieve the effect of moving the gap to logical index, it turns out we just need to shift the difference
    // between the existing gap and desired new gap location. (Moving backwards or `oldIdx > newIdx` should be trivial
    // and is the same as MoveGapToBufferIndex(), therefore it's not illustrated here).
    //
    //              newIdxLogical
    //                    v
    //     *****------*********
    //             ┏━━└──┘
    //             ┃
    //             🠛
    //          ┌──┐
    //     *********------*****
    //              ^      ^
    //              |  newIdxLogical, also gap end
    //          gap begin

    int64_t newIdx = MapLogicalIndexToBufferIndex(buf, newIdxLogical);
    int64_t oldIdx = buf.frontSize;
    int64_t backBegin = buf.GetBackBegin();
    int64_t frontEnd = buf.GetFrontEnd();
    if (oldIdx == newIdx) return;

    if (oldIdx < newIdx) {
        size_t size = newIdx - backBegin;
        memmove(buf.buffer + frontEnd, buf.buffer + backBegin, size);
        buf.frontSize = frontEnd + size;
    } else /* oldIdx > newIdx */ {
        size_t size = oldIdx - newIdx;
        memmove(buf.buffer + backBegin - size, buf.buffer + frontEnd - size, size);
        buf.frontSize = newIdx;
    }
}

void Ionl::WidenGap(Ionl::GapBuffer& buf, size_t requestedGapSize) {
    // Some assumptions:
    // - Increasing the gap size means the user is editing this buffer, which means they'll probably edit it some more
    // - Hence, it's likely that this buffer will be reallocated multiple times in the future
    // - Hence, we round buffer size to a power of 2 to reduce malloc() overhead

    int64_t frontSize = buf.GetFrontSize();
    int64_t backSize = buf.GetBackSize();
    // `GapBuffer::gapSize` will be updated as a result of this function call
    int64_t oldGapSize = buf.GetGapSize();

    int64_t newBufSize = ImUpperPowerOfTwo(buf.bufferSize);
    int64_t minimumBufSize = buf.GetContentSize() + requestedGapSize;
    // TODO keep a reasonable size once we get above e.g. 8KB?
    do {
        newBufSize *= 2;
    } while (newBufSize < minimumBufSize);

    ReallocateBuffer(buf.buffer, newBufSize);

    buf.bufferSize = newBufSize;
    buf.frontSize /*keep intact*/;
    buf.gapSize = newBufSize - frontSize - backSize;

    memmove(
        /*New back's location*/ buf.buffer + frontSize + buf.gapSize,
        /*Old back*/ buf.buffer + frontSize + oldGapSize,
        backSize);
}

void Ionl::InsertAtGap(GapBuffer& buf, const ImWchar* text, size_t size) {
    if (buf.GetGapSize() <= size) {
        // Add 1 to void having a 0-length gap
        WidenGap(buf, size + 1);
    }

    assert(buf.gapSize > size);
    memcpy(buf.buffer + buf.GetGapBegin(), text, size);
    buf.frontSize += size;
    buf.gapSize -= size;
}

void Ionl::InsertAtGap(GapBuffer& buf, const char* text, size_t size) {
    auto numCodepoint = ImTextCountCharsFromUtf8(text, text + size);
    if (buf.GetBackSize() <= numCodepoint) {
        WidenGap(buf, numCodepoint + 1);
    }

    assert(buf.gapSize > numCodepoint);
    const char* remaining;
    ImTextStrFromUtf8NoNullTerminate(buf.buffer + buf.GetGapBegin(), buf.gapSize, text, text + size, &remaining);
    assert(remaining == text + size);
    buf.frontSize += numCodepoint;
    buf.gapSize -= numCodepoint;
}

// clang-format off
auto Ionl::GapBuffer::begin() -> iterator { return iterator(*this); }
auto Ionl::GapBuffer::begin() const -> const_iterator { return cbegin(); }
auto Ionl::GapBuffer::cbegin() const -> const_iterator { return const_iterator(*this); }
auto Ionl::GapBuffer::end() -> iterator { iterator it(*this); it.SetEnd(); return it; }
auto Ionl::GapBuffer::end() const -> const_iterator { return cend(); }
auto Ionl::GapBuffer::cend() const -> const_iterator { const_iterator it(*this); it.SetEnd(); return it; }
// clang-format on
