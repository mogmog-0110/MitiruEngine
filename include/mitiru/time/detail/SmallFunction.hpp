#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "mitiru/debug/TracyZones.hpp"

/// @file detail/SmallFunction.hpp
/// @brief Move-only, header-only SBO type-erased callable for @c void().
///
/// Stores callables with captures up to 48 bytes inline (no heap allocation).
/// Larger captures automatically fall back to a heap allocation.
///
/// Usage example:
/// @code
///   mitiru::time::detail::SmallFunction f{[x = 42]{ printf("%d\n", x); }};
///   f();                  // prints 42
///   SmallFunction g = std::move(f);
///   g();                  // prints 42; f is now empty
///   if (g) { g(); }       // operator bool guards against empty call
/// @endcode
///
/// Thread-safety: NOT thread-safe. Use on a single thread.

namespace mitiru::time::detail {

/// Move-only type-erased callable for @c void().
///
/// Inline buffer is 48 bytes. Callables that exceed 48 bytes in size or
/// require stricter alignment than @c std::max_align_t are heap-allocated
/// transparently.
class SmallFunction {
public:
    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------

    /// Constructs an empty SmallFunction. @c operator bool() returns false.
    SmallFunction() noexcept = default;

    /// Stores @p f. Inline if @c sizeof(F) <= 48 and alignment fits; heap otherwise.
    ///
    /// @tparam F  Any callable type with signature @c void().
    template <typename F,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, SmallFunction>>>
    explicit SmallFunction(F&& f) {
        store(std::forward<F>(f));
    }

    SmallFunction(const SmallFunction&)            = delete;
    SmallFunction& operator=(const SmallFunction&) = delete;

    SmallFunction(SmallFunction&& other) noexcept {
        moveFrom(other);
    }

    SmallFunction& operator=(SmallFunction&& other) noexcept {
        if (this != &other) {
            destroySelf();
            moveFrom(other);
        }
        return *this;
    }

    ~SmallFunction() {
        destroySelf();
    }

    // -----------------------------------------------------------------------
    // Invocation
    // -----------------------------------------------------------------------

    /// Invokes the stored callable. Behavior is undefined if empty.
    void operator()() const {
        MITIRU_ZONE_NAMED("SmallFunction::invoke");
        assert(invoke_ && "SmallFunction: called while empty");
        invoke_(dataPtr());
    }

    /// Returns true if a callable is stored.
    explicit operator bool() const noexcept {
        return invoke_ != nullptr;
    }

private:
    // -----------------------------------------------------------------------
    // Internal layout
    // -----------------------------------------------------------------------

    static constexpr std::size_t kBufSize = 48;

    alignas(std::max_align_t) std::byte m_buf[kBufSize]{};
    bool m_heap = false;

    using InvokeFn  = void (*)(const void*);
    using DestroyFn = void (*)(void*);
    using MoveFn    = void (*)(void* dst, void* src);  // src is nulled after move

    InvokeFn  invoke_  = nullptr;
    DestroyFn destroy_ = nullptr;
    MoveFn    move_    = nullptr;

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    [[nodiscard]] void* dataPtr() noexcept {
        if (m_heap) {
            void* ptr = nullptr;
            std::memcpy(&ptr, m_buf, sizeof(void*));
            return ptr;
        }
        return static_cast<void*>(m_buf);
    }

    [[nodiscard]] const void* dataPtr() const noexcept {
        if (m_heap) {
            void* ptr = nullptr;
            std::memcpy(&ptr, m_buf, sizeof(void*));
            return ptr;
        }
        return static_cast<const void*>(m_buf);
    }

    void destroySelf() noexcept {
        if (!destroy_) { return; }
        destroy_(dataPtr());
        if (m_heap) {
            void* ptr = nullptr;
            std::memcpy(&ptr, m_buf, sizeof(void*));
            ::operator delete(ptr);
        }
        invoke_  = nullptr;
        destroy_ = nullptr;
        move_    = nullptr;
        m_heap   = false;
    }

    void moveFrom(SmallFunction& other) noexcept {
        invoke_  = other.invoke_;
        destroy_ = other.destroy_;
        move_    = other.move_;
        m_heap   = other.m_heap;

        if (other.move_) {
            other.move_(m_buf, other.m_buf);
        }

        other.invoke_  = nullptr;
        other.destroy_ = nullptr;
        other.move_    = nullptr;
        other.m_heap   = false;
    }

    // -----------------------------------------------------------------------
    // store() — inline path
    // -----------------------------------------------------------------------

    template <typename F>
    static constexpr bool kFitsInline =
        sizeof(std::decay_t<F>)  <= kBufSize &&
        alignof(std::decay_t<F>) <= alignof(std::max_align_t);

    template <typename F>
    std::enable_if_t<kFitsInline<F>> store(F&& f) {
        using T = std::decay_t<F>;
        new (static_cast<void*>(m_buf)) T(std::forward<F>(f));
        m_heap   = false;
        invoke_  = [](const void* p) { (*static_cast<const T*>(p))(); };
        destroy_ = [](void* p)       { static_cast<T*>(p)->~T(); };
        move_    = [](void* dst, void* src) {
            new (dst) T(std::move(*static_cast<T*>(src)));
            static_cast<T*>(src)->~T();
        };
    }

    // -----------------------------------------------------------------------
    // store() — heap fallback
    // -----------------------------------------------------------------------

    template <typename F>
    std::enable_if_t<!kFitsInline<F>> store(F&& f) {
        using T = std::decay_t<F>;
        T* ptr = static_cast<T*>(::operator new(sizeof(T)));
        new (ptr) T(std::forward<F>(f));
        std::memcpy(m_buf, &ptr, sizeof(void*));
        m_heap   = true;
        invoke_  = [](const void* p) { (*static_cast<const T*>(p))(); };
        destroy_ = [](void* p)       { static_cast<T*>(p)->~T(); };
        // For heap path, move_ swaps the raw pointer stored in buf
        move_    = [](void* dst, void* src) {
            // src and dst are the m_buf arrays; copy the pointer bytes
            std::memcpy(dst, src, sizeof(void*));
            // Zero src pointer so the moved-from SmallFunction won't double-free
            void* null_ptr = nullptr;
            std::memcpy(src, &null_ptr, sizeof(void*));
        };
    }
};

} // namespace mitiru::time::detail
