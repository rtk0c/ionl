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

void Ionl::MoveGap(Ionl::GapBuffer& buf, size_t newIdx) {
    size_t oldIdx = buf.frontSize;
    if (oldIdx == newIdx) return;

    // NOTE: we must use memmove() because gap size may be smaller than movement distance, in which case the src region and dst region will overlap
    auto frontEnd = buf.buffer + buf.frontSize;
    auto backBegin = buf.buffer + buf.frontSize + buf.gapSize;
    if (oldIdx < newIdx) {
        // Moving forwards

        size_t size = newIdx - oldIdx;
        memmove(frontEnd, backBegin, newIdx - oldIdx);
    } else /* oldIdx > newIdx */ {
        // Moving backwards

        size_t size = oldIdx - newIdx;
        memmove(backBegin - size, frontEnd - size, size);
    }
    buf.frontSize = newIdx;
}

void Ionl::WidenGap(Ionl::GapBuffer& buf, size_t newGapSize) {
    // Some assumptions:
    // - Increasing the gap size means the user is editing this buffer, which means they'll probably edit it some more
    // - Hence, it's likely that this buffer will be reallocated multiple times in the future
    // - Hence, we round buffer size to a power of 2 to reduce malloc() overhead

    size_t frontSize = buf.frontSize;
    size_t oldBackSize = buf.bufferSize - buf.frontSize - buf.gapSize;
    size_t oldGapSize = buf.gapSize;

    size_t newBufSize = ImUpperPowerOfTwo(buf.bufferSize);
    size_t minimumBufSize = buf.bufferSize - buf.gapSize + newBufSize;
    // TODO keep a reasonable size once we get above e.g. 8KB?
    do {
        newBufSize *= 2;
    } while (newBufSize < minimumBufSize);

    ReallocateBuffer(buf.buffer, newBufSize);

    buf.bufferSize = newBufSize;
    buf.frontSize /*keep intact*/;
    buf.gapSize = newBufSize - frontSize - oldBackSize;

    memmove(
        /*New back's location*/ buf.buffer + frontSize + buf.gapSize,
        /*Old back*/ buf.buffer + frontSize + oldGapSize,
        oldBackSize);
}

// clang-format off
auto Ionl::GapBuffer::begin() -> iterator { return iterator(*this); }
auto Ionl::GapBuffer::begin() const -> const_iterator { return cbegin(); }
auto Ionl::GapBuffer::cbegin() const -> const_iterator { return const_iterator(*this); }
auto Ionl::GapBuffer::end() -> iterator { iterator it(*this); it.SetEnd(); return it; }
auto Ionl::GapBuffer::end() const -> const_iterator { return cend(); }
auto Ionl::GapBuffer::cend() const -> const_iterator { const_iterator it(*this); it.SetEnd(); return it; }
// clang-format on
