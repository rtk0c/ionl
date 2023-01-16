#pragma once

#include "imgui.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Ionl {

struct GapBuffer {
    ImWchar* buffer;
    int64_t bufferSize;
    int64_t frontSize;
    int64_t gapSize;

    GapBuffer();
    GapBuffer(std::string_view content);
    GapBuffer(GapBuffer&&);
    GapBuffer& operator=(GapBuffer&&);
    ~GapBuffer();

    ImWchar* begin() { return buffer; }
    ImWchar* end() { return buffer + bufferSize; }

    const ImWchar* begin() const { return buffer; }
    const ImWchar* end() const { return buffer + bufferSize; }

    const ImWchar* cbegin() const { return buffer; }
    const ImWchar* cend() const { return buffer + bufferSize; }

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

struct GapBufferIterator {
    GapBuffer* obj;
    // We use signed here to avoid all the Usual Arithmetic Conversion issues, where when doing `signed + unsigned`, both operands get converted to unsigned when we expected "delta"-ing behavior
    // Note that even though `signed = signed + unsigned` does work if both operands have the same width due to wraparound arithmetic, and the fact that the rhs is immediately converted to signed
    // But expressions like `(signed + unsigned) > constant` breaks our intuition because the lhs stays unsigned before entering operator>
    int64_t idx; // Buffer index

    explicit GapBufferIterator(GapBuffer& buffer)
        : obj{ &buffer }, idx{ 0 } {}

    void SetBegin();
    void SetEnd();

    ImWchar& operator*() const;

    GapBufferIterator& operator++();
    GapBufferIterator operator+(int64_t advance) const;
    GapBufferIterator& operator+=(int64_t advance);

    GapBufferIterator& operator--();
    GapBufferIterator operator-(int64_t advance) const;
    GapBufferIterator& operator-=(int64_t advance);

    bool HasNext() const;
    bool operator<(const GapBufferIterator& that) const;
    bool operator>(const GapBufferIterator& that) const;
    bool operator==(const GapBufferIterator& that) const;

private:
    GapBufferIterator()
        : obj{ nullptr }, idx{ 0 } {}
};

} // namespace Ionl
