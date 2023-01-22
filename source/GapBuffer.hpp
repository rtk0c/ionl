#pragma once

#include "Utils.hpp"
#include "imgui.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Ionl {

template <typename TContainer>
struct GapBufferIterator;

struct GapBuffer {
    using iterator = GapBufferIterator<GapBuffer>;
    using const_iterator = GapBufferIterator<const GapBuffer>;

    ImWchar* buffer;
    int64_t bufferSize;
    int64_t frontSize;
    int64_t gapSize;

    GapBuffer();
    GapBuffer(std::string_view content);
    GapBuffer(GapBuffer&&) noexcept;
    GapBuffer& operator=(GapBuffer&&) noexcept;
    ~GapBuffer();

    iterator begin();
    const_iterator begin() const;
    const_iterator cbegin() const;

    iterator end();
    const_iterator end() const;
    const_iterator cend() const;

    ImWchar* PtrBegin() { return buffer; }
    ImWchar* PtrEnd() { return buffer + bufferSize; }

    const ImWchar* PtrBegin() const { return buffer; }
    const ImWchar* PtrEnd() const { return buffer + bufferSize; }

    const ImWchar* PtrCBegin() const { return buffer; }
    const ImWchar* PtrCEnd() const { return buffer + bufferSize; }

    int64_t GetContentSize() const { return bufferSize - gapSize; }

    int64_t GetFrontBegin() const { return 0; }
    int64_t GetFrontEnd() const { return GetGapBegin(); }
    int64_t GetFrontSize() const { return GetFrontEnd() - GetFrontBegin(); }

    int64_t GetGapBegin() const { return frontSize; }
    int64_t GetGapEnd() const { return GetBackBegin(); }
    int64_t GetGapSize() const { return GetGapEnd() - GetGapBegin(); }

    int64_t GetBackBegin() const { return frontSize + gapSize; }
    int64_t GetBackEnd() const { return bufferSize; }
    int64_t GetBackSize() const { return GetBackEnd() - GetBackBegin(); }

    const ImWchar& operator[](size_t i) const { return i > frontSize ? buffer[i + gapSize] : buffer[i]; }
    ImWchar& operator[](size_t i) { return const_cast<ImWchar&>(const_cast<const GapBuffer&>(*this)[i]); }

    std::string ExtractContent() const;
    void UpdateContent(std::string_view content);
};

int64_t MapLogicalIndexToBufferIndex(const GapBuffer& buffer, int64_t logicalIdx);

// If the buffer index does not point to a valid logical location (i.e. it points to somewhere in the gap), -1 is returned
int64_t MapBufferIndexToLogicalIndex(const GapBuffer& buffer, int64_t bufferIdx);

int64_t AdjustBufferIndex(const GapBuffer& buffer, int64_t /*buffer index*/ idx, int64_t delta);

void MoveGap(GapBuffer& buf, size_t newIdx);
void WidenGap(GapBuffer& buf, size_t newGapSize = 0);

template <typename TContainer>
struct GapBufferIterator {
    TContainer* obj;
    // We use signed here to avoid all the Usual Arithmetic Conversion issues, where when doing `signed + unsigned`, both operands get converted to unsigned when we expected "delta"-ing behavior
    // Note that even though `signed = signed + unsigned` does work if both operands have the same width due to wraparound arithmetic, and the fact that the rhs is immediately converted to signed
    // But expressions like `(signed + unsigned) > constant` breaks our intuition because the lhs stays unsigned before entering operator>
    int64_t idx; // Buffer index

    explicit GapBufferIterator(TContainer& buffer)
        : obj{ &buffer }, idx{ 0 } {}

    explicit GapBufferIterator(TContainer& buffer, int64_t bufferIdx)
        : obj{ &buffer }, idx{ bufferIdx } {}

    void SetBegin() { idx = 0; }
    void SetEnd() { idx = obj->bufferSize; }

    ImWchar& operator*() const { return obj->buffer[idx]; }

    GapBufferIterator& operator++() {
        idx += 1;
        if (idx == obj->frontSize) {
            idx += obj->gapSize;
        }
        return *this;
    }

    GapBufferIterator operator+(int64_t advance) const { return GapBufferIterator(*obj, AdjustBufferIndex(*obj, idx, advance)); }
    GapBufferIterator& operator+=(int64_t advance) { return *this = *this + advance; }

    GapBufferIterator& operator--() {
        if (idx == obj->frontSize + obj->gapSize) {
            idx -= obj->gapSize;
        } else {
            idx -= 1;
        }
        return *this;
    }

    GapBufferIterator operator-(int64_t advance) const { return *this + (advance); }
    GapBufferIterator& operator-=(int64_t advance) { return *this += (-advance); }

    bool HasNext() const { return idx != obj->bufferSize; }

    bool operator<(const GapBufferIterator& that) const { return this->idx < that.idx; }
    bool operator>(const GapBufferIterator& that) const { return this->idx > that.idx; }
    bool operator==(const GapBufferIterator& that) const { return this->idx == that.idx; }
};

} // namespace Ionl
