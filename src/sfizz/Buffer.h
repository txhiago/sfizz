// SPDX-License-Identifier: BSD-2-Clause

// Copyright (c) 2019-2020, Paul Ferrand, Andrea Zanellato
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once
#include "Config.h"
#include "utility/LeakDetector.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>
#include <algorithm>
#include <utility>
#include <atomic>
#if !defined(_WIN32)
#include <sys/mman.h>
#endif

#ifdef DEBUG
#include <iostream>
#endif
namespace sfz
{

/**
 * @brief      A buffer counting class that tries to track the memory usage.
 */
class BufferCounter
{
public:
    /**
     * @brief      Return the buffer counter object.
     */
    static BufferCounter& counter() noexcept
    {
        static BufferCounter counter;
        return counter;
    }

protected:
    BufferCounter() noexcept = default;
    ~BufferCounter() noexcept
    {
#ifndef NDEBUG
        std::cout << "Remaining buffers on destruction: " << numBuffers << '\n';
        std::cout << "Total size: " << bytes << '\n';
#endif
    }

public:
    template <class I>
    void newBuffer(I size) noexcept
    {
        ++numBuffers;
        bytes.fetch_add(static_cast<size_t>(size));
    }

    template <class I>
    void bufferResized(I oldSize, I newSize) noexcept
    {
        bytes.fetch_add(static_cast<size_t>(newSize));
        bytes.fetch_sub(static_cast<size_t>(oldSize));
    }

    template <class I>
    void bufferDeleted(I size) noexcept
    {
        --numBuffers;
        bytes.fetch_sub(static_cast<size_t>(size));
    }

    size_t getNumBuffers() const noexcept { return numBuffers; }
    size_t getTotalBytes() const noexcept { return bytes; }
private:
    std::atomic<size_t> numBuffers { 0 };
    std::atomic<size_t> bytes { 0 };
};



/**
 * @brief      Where a Buffer's memory comes from: the heap, zero-filled, as
 *             sfizz always allocated it. Its memory cannot be locked.
 *
 * An allocator says through `mapped` whether a block is pages of its own
 * (PageAllocator's) or heap memory, and is handed it back to release or lock
 * the block: a PageAllocator falls back to the heap.
 */
struct HeapAllocator {
    static void* allocate(size_t bytes, bool& mapped) noexcept
    {
        mapped = false;
        return std::calloc(bytes, 1);
    }
    static void release(void* p, size_t, bool) noexcept { std::free(p); }
    static bool lock(void*, size_t, bool) noexcept { return false; }
};

/**
 * @brief      Where a Buffer's memory comes from: pages of its own, mapped
 *             for it alone and zero-filled, so that they can be locked in
 *             memory without locking a neighbor's (mlock works by the page,
 *             and does not count), and are unlocked when they are unmapped.
 *             For sample data the rendering thread reads (StageKeys #221):
 *             the kernel may otherwise swap it out, and the rendering
 *             thread then waits for it to come back. Where pages cannot be
 *             mapped, it falls back to the heap, whose memory is not locked;
 *             on Windows it is the heap.
 */
#if defined(_WIN32)
using PageAllocator = HeapAllocator;
#else
struct PageAllocator {
    static void* allocate(size_t bytes, bool& mapped) noexcept
    {
        void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        mapped = p != MAP_FAILED;
        // Refused pages (an address space or a map count used up) are no reason
        // to fail a load the heap can still serve: that memory plays unlocked.
        return mapped ? p : HeapAllocator::allocate(bytes, mapped);
    }
    static void release(void* p, size_t bytes, bool mapped) noexcept
    {
        if (mapped)
            munmap(p, bytes);
        else
            std::free(p);
    }
    static bool lock(void* p, size_t bytes, bool mapped) noexcept
    {
        return mapped && mlock(p, bytes) == 0;
    }
};
#endif

/**
 * @brief      A heap buffer structure that tries to align its beginning and
 *             adds a small offset at the end for alignment too.
 *
 *             Apparently on Linux this effort is mostly useless, and in the end
 *             most of the SIMD operations are coded with alignment checks and
 *             sentinels so this class could probably be much simpler. It does
 *             however wrap realloc which in some cases should be a bit more
 *             efficient than allocating a whole new block.
 *
 * @tparam     Type       The buffer type
 * @tparam     Alignment  the required alignment in bytes (defaults to
 *                        config::defaultAlignment)
 * @tparam     Allocator  where the memory comes from (HeapAllocator, or
 *                        PageAllocator for memory that can be locked)
 */
template <class Type, unsigned int Alignment = config::defaultAlignment, class Allocator = HeapAllocator>
class Buffer {
public:
    using value_type = typename std::remove_cv<Type>::type;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using reference = value_type&;
    using const_reference = const value_type&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using size_type = size_t;

    /**
     * @brief Construct a new Buffer object that is empty
     *
     */
    Buffer() noexcept
    {
    }

    /**
     * @brief Construct a new Buffer object with size
     *
     * @param size
     */
    explicit Buffer(size_t size)
    {
        resize(size);
    }

    /**
     * @brief Resizes the buffer. Given that std::realloc may return either the same pointer
     * or a new one, you need to account for both cases in the code if you are using deep pointers.
     *
     * @param newSize the new buffer size in bytes
     * @return true if allocation succeeded
     * @return false otherwise
     */
    bool resize(size_t newSize, std::nothrow_t) noexcept
    {
        if (newSize == 0) {
            clear();
            return true;
        }

        Type* oldData = paddedData.get();
        Type* oldNormalData = normalData;
        std::size_t oldSize = alignedSize;
        std::size_t oldBytes = paddedData.get_deleter().bytes;
        bool oldMapped = paddedData.get_deleter().mapped;

        std::size_t tempSize = newSize + 2 * AlignmentMask; // To ensure that we have leeway at the beginning and at the end
        bool newMapped = false;
        Type* newData = reinterpret_cast<Type*>(Allocator::allocate(tempSize * sizeof(value_type), newMapped));
        if (newData == nullptr) {
            return false;
        }
        // Memory that was locked stays locked when it moves.
        if (locked)
            locked = Allocator::lock(newData, tempSize * sizeof(value_type), newMapped);

        if (largerSize > 0)
            counter().bufferResized(largerSize * sizeof(value_type), tempSize * sizeof(value_type));
        else
            counter().newBuffer(tempSize * sizeof(value_type));

        largerSize = tempSize;
        alignedSize = newSize;
        paddedData.release(); // realloc has invalidated the old pointer
        paddedData.reset(static_cast<pointer>(newData));
        paddedData.get_deleter().bytes = tempSize * sizeof(value_type);
        paddedData.get_deleter().mapped = newMapped;
        normalData = static_cast<pointer>(align(Alignment, alignedSize, newData, tempSize));
        normalEnd = normalData + alignedSize;
        std::size_t endMisalignment = (alignedSize & TypeAlignmentMask);
        if (endMisalignment != 0)
            _alignedEnd = normalEnd + Alignment - endMisalignment;
        else
            _alignedEnd = normalEnd;

        // On a first allocation there is nothing to keep, and oldNormalData is
        // null: memcpy's arguments must be valid pointers even for a zero size.
        if (oldNormalData != nullptr)
            std::memcpy(normalData, oldNormalData, std::min(newSize, oldSize) * sizeof(Type));
        if (oldData != nullptr)
            Allocator::release(oldData, oldBytes, oldMapped);

        return true;
    }

    /**
     * @brief Resizes the buffer. Given that std::realloc may return either the same pointer
     * or a new one, you need to account for both cases in the code if you are using deep pointers.
     *
     * @param newSize the new buffer size in bytes
     */
    void resize(size_t newSize)
    {
        if (!resize(newSize, std::nothrow))
            throw std::bad_alloc();
    }

    /**
     * @brief Clear the buffers and frees the underlying memory
     *
     */
    void clear() noexcept
    {
        if (largerSize > 0)
            counter().bufferDeleted(largerSize * sizeof(value_type));
        _clear();
    }
    ~Buffer() noexcept
    {
        if (largerSize > 0)
            counter().bufferDeleted(largerSize * sizeof(value_type));
    }

    /**
     * @brief Locks the buffer's memory, so that it is never swapped out, until
     * it is freed. Only a PageAllocator's mapped memory can be locked, not the
     * heap it falls back to; a buffer resized later stays locked.
     *
     * @return true if the memory is locked (an empty buffer is, trivially)
     */
    bool lockInMemory() noexcept
    {
        if (!paddedData)
            return true;
        const deleter& block = paddedData.get_deleter();
        locked = Allocator::lock(paddedData.get(), block.bytes, block.mapped);
        return locked;
    }

    /**
     * @brief Get the size of the larger block which is actually allocated
     */
    size_t allocationSize() const noexcept
    {
        return largerSize;
    }

    /**
     * @brief Construct a new Buffer object from an existing one
     *
     * @param other
     */
    Buffer(const Buffer& other)
    {
        resize(other.size());
        if (other.size() != 0) // an empty buffer's data() is null
            std::memcpy(this->data(), other.data(), other.size() * sizeof(value_type));
    }

    /**
     * @brief Construct a new Buffer object by moving an existing one
     *
     * @param other
     */
    Buffer(Buffer&& other) noexcept
        : largerSize(other.largerSize),
          alignedSize(other.alignedSize),
          normalData(other.normalData),
          paddedData(std::move(other.paddedData)),
          normalEnd(other.normalEnd),
          _alignedEnd(other._alignedEnd),
          locked(other.locked)
    {
        other._clear();
    }

    Buffer& operator=(const Buffer& other)
    {
        if (this != &other) {
            resize(other.size());
            if (other.size() != 0) // an empty buffer's data() is null
                std::memcpy(this->data(), other.data(), other.size() * sizeof(value_type));
        }
        return *this;
    }

    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other) {
            if (largerSize > 0)
                counter().bufferDeleted(largerSize * sizeof(value_type));
            largerSize = other.largerSize;
            alignedSize = other.alignedSize;
            normalData = other.normalData;
            paddedData = std::move(other.paddedData);
            normalEnd = other.normalEnd;
            _alignedEnd = other._alignedEnd;
            locked = other.locked;
            other._clear();
        }
        return *this;
    }

    Type& operator[](size_t idx) noexcept { return *(normalData + idx); }
    constexpr pointer data() const noexcept { return normalData; }
    constexpr size_type size() const noexcept { return alignedSize; }
    constexpr bool empty() const noexcept { return alignedSize == 0; }
    constexpr iterator begin() const noexcept  { return data(); }
    constexpr iterator end() const noexcept { return normalEnd; }
    constexpr pointer alignedEnd() const noexcept { return _alignedEnd; }

    /**
     * @brief      Return the buffer counter object.
     *
     * @return     The buffer counter on which you can call various methods.
     */
    static BufferCounter& counter() noexcept
    {
        return BufferCounter::counter();
    }

private:
    void _clear()
    {
        largerSize = 0;
        alignedSize = 0;
        paddedData.reset();
        paddedData.get_deleter().bytes = 0;
        paddedData.get_deleter().mapped = false;
        normalData = nullptr;
        normalEnd = nullptr;
        _alignedEnd = nullptr;
        locked = false;
    }

private:
    static constexpr int AlignmentMask { Alignment - 1 };
    static constexpr int TypeAlignment { Alignment / sizeof(value_type) };
    static constexpr int TypeAlignmentMask { TypeAlignment - 1 };
    static_assert(std::is_trivial<value_type>::value, "Type should be trivial");
    static_assert(Alignment == 0 || Alignment == 4 || Alignment == 8 || Alignment == 16 || Alignment == 32, "Bad alignment value");
    static_assert(TypeAlignment * sizeof(value_type) == Alignment || !std::is_arithmetic<value_type>::value,
                  "The alignment does not appear to be divided by the size of the arithmetic Type");
    void* align(std::size_t alignment, std::size_t size, void *ptr, std::size_t &space)
    {
        std::uintptr_t pn = reinterpret_cast<std::uintptr_t>(ptr);
        std::uintptr_t aligned = (pn + alignment - 1) & - alignment;
        std::size_t padding = aligned - pn;
        if (space < size + padding) return nullptr;
        space -= padding;
        return reinterpret_cast<void *>(aligned);
    }

    // Unmapping needs the size, which free() does not, and a PageAllocator's
    // block may be heap memory instead: the deleter carries both.
    struct deleter {
        size_t bytes { 0 };
        bool mapped { false };
        void operator()(void *p) const noexcept { Allocator::release(p, bytes, mapped); }
    };

    size_type largerSize { 0 };
    size_type alignedSize { 0 };
    pointer normalData { nullptr };
    std::unique_ptr<value_type[], deleter> paddedData;
    pointer normalEnd { nullptr };
    pointer _alignedEnd { nullptr };
    bool locked { false };
    LEAK_DETECTOR(Buffer);
};

}
