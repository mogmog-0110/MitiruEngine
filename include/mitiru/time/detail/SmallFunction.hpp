#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "mitiru/debug/TracyZones.hpp"

/// @file detail/SmallFunction.hpp
/// @brief @c void() 用の move-only・header-only な SBO type-erased callable。
///
/// キャプチャが 48 byte までの callable をインラインに格納する (heap allocation 無し)。
/// より大きいキャプチャは自動的に heap allocation にフォールバックする。
///
/// 使用例:
/// @code
///   mitiru::time::detail::SmallFunction f{[x = 42]{ printf("%d\n", x); }};
///   f();                  // prints 42
///   SmallFunction g = std::move(f);
///   g();                  // prints 42; f is now empty
///   if (g) { g(); }       // operator bool guards against empty call
/// @endcode
///
/// スレッド安全性: thread-safe ではない。単一スレッドで使うこと。

namespace mitiru::time::detail {

/// @c void() 用の move-only な type-erased callable。
///
/// インライン buffer は 48 byte。サイズが 48 byte を超える、または
/// @c std::max_align_t より厳しい alignment を要求する callable は、
/// 透過的に heap-allocate される。
class SmallFunction {
public:
    // -----------------------------------------------------------------------
    // 構築 / 破棄
    // -----------------------------------------------------------------------

    /// 空の SmallFunction を構築する。@c operator bool() は false を返す。
    SmallFunction() noexcept = default;

    /// @p f を格納する。@c sizeof(F) <= 48 で alignment が合えばインライン、それ以外は heap。
    ///
    /// @tparam F  シグネチャ @c void() の任意の callable 型。
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
    // 呼び出し
    // -----------------------------------------------------------------------

    /// 格納された callable を呼び出す。空の場合は未定義動作。
    void operator()() const {
        MITIRU_ZONE_NAMED("SmallFunction::invoke");
        assert(invoke_ && "SmallFunction: called while empty");
        invoke_(dataPtr());
    }

    /// callable が格納されていれば true を返す。
    explicit operator bool() const noexcept {
        return invoke_ != nullptr;
    }

private:
    // -----------------------------------------------------------------------
    // 内部レイアウト
    // -----------------------------------------------------------------------

    static constexpr std::size_t kBufSize = 48;

    alignas(std::max_align_t) std::byte m_buf[kBufSize]{};
    bool m_heap = false;

    using InvokeFn  = void (*)(const void*);
    using DestroyFn = void (*)(void*);
    using MoveFn    = void (*)(void* dst, void* src);  // move 後 src は null 化される

    InvokeFn  invoke_  = nullptr;
    DestroyFn destroy_ = nullptr;
    MoveFn    move_    = nullptr;

    // -----------------------------------------------------------------------
    // ヘルパー
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
    // store() — インラインパス
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
    // store() — heap フォールバック
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
        // heap パスでは move_ は buf に格納された生 pointer を入れ替える
        move_    = [](void* dst, void* src) {
            // src と dst は m_buf 配列。pointer の byte をコピーする
            std::memcpy(dst, src, sizeof(void*));
            // moved-from の SmallFunction が double-free しないよう src pointer を 0 化
            void* null_ptr = nullptr;
            std::memcpy(src, &null_ptr, sizeof(void*));
        };
    }
};

} // namespace mitiru::time::detail
