// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "sfizz/Buffer.h"
#include "catch2/catch.hpp"
#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>
using namespace Catch::literals;

TEST_CASE("[Buffer] Empty (float)")
{
    sfz::Buffer<float> emptyBuffer;
    REQUIRE(emptyBuffer.empty());
    REQUIRE(emptyBuffer.size() == 0);
}

TEST_CASE("[Buffer] Empty (int)")
{
    sfz::Buffer<int> emptyBuffer;
    REQUIRE(emptyBuffer.empty());
    REQUIRE(emptyBuffer.size() == 0);
}

TEST_CASE("[Buffer] Empty (double)")
{
    sfz::Buffer<double> emptyBuffer;
    REQUIRE(emptyBuffer.empty());
    REQUIRE(emptyBuffer.size() == 0);
}

TEST_CASE("[Buffer] Empty (uint8_t)")
{
    sfz::Buffer<uint8_t> emptyBuffer;
    REQUIRE(emptyBuffer.empty());
    REQUIRE(emptyBuffer.size() == 0);
}

template <class Type>
void checkBoundaries(sfz::Buffer<Type>& buffer, int expectedSize)
{
    REQUIRE((int)buffer.size() == expectedSize);
    REQUIRE(((size_t)buffer.data() & (sfz::config::defaultAlignment - 1)) == 0);
    REQUIRE(((size_t)buffer.alignedEnd() & (sfz::config::defaultAlignment - 1)) == 0);
    REQUIRE(std::distance(buffer.begin(), buffer.end()) == expectedSize);
    REQUIRE(std::distance(buffer.begin(), buffer.alignedEnd()) >= expectedSize);
}

TEST_CASE("[Buffer] 10 floats ")
{
    const int baseSize { 10 };
    sfz::Buffer<float> buffer(baseSize);
    checkBoundaries(buffer, baseSize);

    for (auto& element : buffer)
        element = 0.0f;
    for (auto& element : buffer)
        REQUIRE(element == 0.0f);
}

TEST_CASE("[Buffer] Resize 10 floats ")
{
    const int baseSize { 10 };
    const int smallSize { baseSize / 2 };
    const int bigSize { baseSize * 2 };
    sfz::Buffer<float> buffer(baseSize);
    REQUIRE(!buffer.empty());
    checkBoundaries(buffer, baseSize);

    std::fill(buffer.begin(), buffer.end(), 1.0f);

    REQUIRE(buffer.resize(smallSize, std::nothrow));
    checkBoundaries(buffer, smallSize);

    REQUIRE(std::all_of(buffer.begin(), buffer.end(), [](float value) { return value == 1.0f; }));

    REQUIRE(buffer.resize(bigSize, std::nothrow));
    checkBoundaries(buffer, bigSize);
    for (auto i = 0; i < smallSize; ++i)
        REQUIRE(buffer[i] == 1.0f);
}

TEST_CASE("[Buffer] Resize 4096 floats ")
{
    const int baseSize { 4096 };
    const int smallSize { baseSize / 2 };
    const int bigSize { baseSize * 2 };
    sfz::Buffer<float> buffer(baseSize);
    REQUIRE(!buffer.empty());
    checkBoundaries(buffer, baseSize);

    std::fill(buffer.begin(), buffer.end(), 1.0f);

    REQUIRE(buffer.resize(smallSize, std::nothrow));
    checkBoundaries(buffer, smallSize);

    REQUIRE(std::all_of(buffer.begin(), buffer.end(), [](float value) { return value == 1.0f; }));

    REQUIRE(buffer.resize(bigSize, std::nothrow));
    checkBoundaries(buffer, bigSize);
    for (auto i = 0; i < smallSize; ++i)
        REQUIRE(buffer[i] == 1.0f);
}

TEST_CASE("[Buffer] Resize 65536 floats ")
{
    const int baseSize { 10 };
    const int smallSize { baseSize / 2 };
    const int bigSize { baseSize * 2 };
    sfz::Buffer<float> buffer(baseSize);
    REQUIRE(!buffer.empty());
    checkBoundaries(buffer, baseSize);

    std::fill(buffer.begin(), buffer.end(), 1.0f);

    REQUIRE(buffer.resize(smallSize, std::nothrow));
    checkBoundaries(buffer, smallSize);

    REQUIRE(std::all_of(buffer.begin(), buffer.end(), [](float value) { return value == 1.0f; }));

    REQUIRE(buffer.resize(bigSize, std::nothrow));
    checkBoundaries(buffer, bigSize);
    for (auto i = 0; i < smallSize; ++i)
        REQUIRE(buffer[i] == 1.0f);
}

TEST_CASE("[Buffer] Copy and move")
{
    const int baseSize { 128 };
    sfz::Buffer<float> buffer(baseSize);
    sfz::Buffer<float> copied { baseSize - 4 };
    std::fill(buffer.begin(), buffer.end(), 1.0f);
    std::fill(copied.begin(), copied.end(), 2.0f);
    copied = buffer;
    checkBoundaries(copied, baseSize);
    REQUIRE(std::all_of(copied.begin(), copied.end(), [](float value) { return value == 1.0f; }));

    sfz::Buffer<float> copyConstructed { buffer };
    checkBoundaries(copyConstructed, baseSize);
    REQUIRE(std::all_of(copyConstructed.begin(), copyConstructed.end(), [](float value) { return value == 1.0f; }));

    sfz::Buffer<float> moveConstructed { std::move(buffer) };
    REQUIRE(buffer.empty());
    checkBoundaries(moveConstructed, baseSize);
    REQUIRE(std::all_of(moveConstructed.begin(), moveConstructed.end(), [](float value) { return value == 1.0f; }));
}

TEST_CASE("[Buffer] Buffer counter")
{
    sfz::BufferCounter& counter = sfz::Buffer<float>::counter();

    // handle the eventuality that the buffer counter does not start at zero
    const size_t initialNumBuffers = counter.getNumBuffers();
    const size_t initialTotalBytes = counter.getTotalBytes();
    auto haveNumBuffers = [&](size_t n) -> bool {
        return n == counter.getNumBuffers() - initialNumBuffers;
    };
    auto haveTotalAllocation = [&](size_t n) -> bool {
        return n * sizeof(float) == counter.getTotalBytes() - initialTotalBytes;
    };

    // create an empty buffer
    sfz::Buffer<float> b1;
    REQUIRE(haveNumBuffers(0));
    REQUIRE(haveTotalAllocation(0));

    // clear an empty buffer
    b1.clear();
    REQUIRE(haveNumBuffers(0));
    REQUIRE(haveTotalAllocation(0));

    // create a sized buffer
    sfz::Buffer<float> b2(5);
    REQUIRE(haveNumBuffers(1));
    REQUIRE(haveTotalAllocation(b2.allocationSize()));

    // resize an empty buffer
    b1.resize(3);
    REQUIRE(haveNumBuffers(2));
    REQUIRE(haveTotalAllocation(b1.allocationSize() + b2.allocationSize()));

    // resize a non-empty buffer
    b1.resize(7);
    REQUIRE(haveNumBuffers(2));
    REQUIRE(haveTotalAllocation(b1.allocationSize() + b2.allocationSize()));

    // clear a non-empty buffer
    b2.clear();
    REQUIRE(haveNumBuffers(1));
    REQUIRE(haveTotalAllocation(b1.allocationSize()));

    // move an empty buffer into a non-empty one
    b1 = std::move(b2);
    REQUIRE(haveNumBuffers(0));
    REQUIRE(haveTotalAllocation(0));

    // move a non-empty buffer into an empty one
    b1.resize(3);
    b2 = std::move(b1);
    REQUIRE(haveNumBuffers(1));
    REQUIRE(haveTotalAllocation(b2.allocationSize()));
}

namespace {
// Hands out mapped and heap blocks in turn, as a PageAllocator does when some
// pages cannot be mapped (StageKeys #221), and checks that each block is
// released and locked with the kind it was allocated as.
struct AlternatingAllocator {
    static int allocations;
    static int releases;
    static int mismatches;
    static std::vector<std::pair<void*, bool>> live;
    static void* allocate(size_t bytes, bool& mapped) noexcept
    {
        mapped = (allocations++ % 2) == 0;
        void* p = std::calloc(bytes, 1);
        live.emplace_back(p, mapped);
        return p;
    }
    static bool kindOf(void* p, bool& mapped)
    {
        auto it = std::find_if(live.begin(), live.end(), [p](const std::pair<void*, bool>& b) { return b.first == p; });
        if (it == live.end())
            return false;
        mapped = it->second;
        return true;
    }
    static void release(void* p, size_t, bool mapped) noexcept
    {
        bool kind = false;
        if (!kindOf(p, kind) || kind != mapped)
            ++mismatches;
        live.erase(std::remove_if(live.begin(), live.end(), [p](const std::pair<void*, bool>& b) { return b.first == p; }), live.end());
        ++releases;
        std::free(p);
    }
    static bool lock(void* p, size_t, bool mapped) noexcept
    {
        bool kind = false;
        if (!kindOf(p, kind) || kind != mapped)
            ++mismatches;
        return mapped;
    }
};
int AlternatingAllocator::allocations = 0;
int AlternatingAllocator::releases = 0;
int AlternatingAllocator::mismatches = 0;
std::vector<std::pair<void*, bool>> AlternatingAllocator::live;
} // namespace

TEST_CASE("[Buffer] A block is released and locked as the kind it was allocated")
{
    using TestBuffer = sfz::Buffer<float, sfz::config::defaultAlignment, AlternatingAllocator>;
    {
        TestBuffer a(64); // mapped
        TestBuffer b(64); // heap
        REQUIRE(a.lockInMemory());
        REQUIRE_FALSE(b.lockInMemory());
        a.resize(128); // mapped: the lock moves with it
        REQUIRE(a.lockInMemory());
        a.resize(256); // heap: it cannot stay locked
        REQUIRE_FALSE(a.lockInMemory());
        a.resize(512); // mapped again
        REQUIRE(a.lockInMemory());
        b = std::move(a); // b's heap block is released, a's mapped one moves
        REQUIRE(b.lockInMemory());
        TestBuffer c(std::move(b));
        REQUIRE(c.lockInMemory());
        c.clear();
        REQUIRE(c.lockInMemory()); // an empty buffer is trivially locked
    }
    REQUIRE(AlternatingAllocator::mismatches == 0);
    REQUIRE(AlternatingAllocator::live.empty());
    REQUIRE(AlternatingAllocator::releases == AlternatingAllocator::allocations);
}

#if !defined(_WIN32)
TEST_CASE("[Buffer] PageAllocator's memory can be locked")
{
    sfz::Buffer<float, sfz::config::defaultAlignment, sfz::PageAllocator> buffer(4096);
    REQUIRE(buffer.lockInMemory());
    buffer.resize(8192);
    REQUIRE(buffer.size() == 8192);
    REQUIRE(buffer.lockInMemory());
}
#endif
