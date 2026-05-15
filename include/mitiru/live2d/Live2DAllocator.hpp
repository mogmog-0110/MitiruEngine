#pragma once

/// @file Live2DAllocator.hpp
/// @brief ICubismAllocator implementation using standard C++ allocators

#ifdef MITIRU_HAS_CUBISM

#include <cstdlib>
#include <cstring>

#include <CubismFramework.hpp>
#include <ICubismAllocator.hpp>

namespace mitiru::live2d
{

/// @brief Standard C++ memory allocator for Cubism Framework
/// @details Uses std::malloc/std::free for normal allocations
///          and platform-aligned allocation for aligned requests.
class Live2DAllocator final : public Csm::ICubismAllocator
{
public:
    void* Allocate(const Csm::csmSizeType size) override
    {
        return std::malloc(static_cast<std::size_t>(size));
    }

    void Deallocate(void* memory) override
    {
        std::free(memory);
    }

    void* AllocateAligned(const Csm::csmSizeType size, const Csm::csmUint32 alignment) override
    {
#ifdef _MSC_VER
        return _aligned_malloc(static_cast<std::size_t>(size),
                               static_cast<std::size_t>(alignment));
#else
        void* ptr = nullptr;
        if (posix_memalign(&ptr, static_cast<std::size_t>(alignment),
                           static_cast<std::size_t>(size)) != 0)
        {
            return nullptr;
        }
        return ptr;
#endif
    }

    void DeallocateAligned(void* alignedMemory) override
    {
#ifdef _MSC_VER
        _aligned_free(alignedMemory);
#else
        std::free(alignedMemory);
#endif
    }
};

} // namespace mitiru::live2d

#endif // MITIRU_HAS_CUBISM
