#pragma once

/// @file GpuTimer.hpp
/// @brief GPUタイマークエリ計測
/// @details GPUレンダリングパスごとの処理時間を計測する。
///          DX11ではタイムスタンプクエリ、OpenGLではGL_TIME_ELAPSEDを使用する。
///          FrameBudgetと統合してGPU時間をレポートする。

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#ifdef MITIRU_HAS_DX11
#include <d3d11.h>
#include <wrl/client.h>
#endif

#ifdef MITIRU_HAS_OPENGL
#include <GL/glew.h>
#endif

namespace mitiru::debug
{

/// @brief GPUレンダリングパスの計測
/// @details 各パスのGPU処理時間をタイムスタンプクエリで計測する。
///
/// @code
/// GpuTimer timer;
/// timer.init(device);
/// // レンダリングループ:
/// timer.beginPass(GpuTimer::Pass::Main3D);
/// // ... 描画 ...
/// timer.endPass(GpuTimer::Pass::Main3D);
/// timer.resolve();
/// float gpuMs = timer.totalGpuMs();
/// @endcode
class GpuTimer
{
public:
    /// @brief GPUレンダリングパス
    enum class Pass : std::uint8_t
    {
        Shadow,
        Main3D,
        PostProcess,
        UI2D,
        Count
    };

    /// @brief パス数
    static constexpr std::size_t kPassCount =
        static_cast<std::size_t>(Pass::Count);

    /// @brief コンストラクタ
    GpuTimer() noexcept
    {
        m_passMs.fill(0.0f);
        m_passActive.fill(false);
    }

    /// @brief デストラクタ
    ~GpuTimer()
    {
        shutdown();
    }

    /// コピー禁止
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    /// ムーブ禁止
    GpuTimer(GpuTimer&&) = delete;
    GpuTimer& operator=(GpuTimer&&) = delete;

    // ── 初期化 ───────────────────────────────────────────────

#ifdef MITIRU_HAS_DX11

    /// @brief DX11デバイスで初期化する
    /// @param device DX11デバイス
    /// @param context デバイスコンテキスト
    /// @return 初期化成功時 true
    bool init(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        if (!device || !context)
        {
            return false;
        }
        m_d3dContext = context;

        D3D11_QUERY_DESC disjointDesc{};
        disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

        HRESULT hr = device->CreateQuery(&disjointDesc, &m_disjointQuery);
        if (FAILED(hr))
        {
            return false;
        }

        D3D11_QUERY_DESC tsDesc{};
        tsDesc.Query = D3D11_QUERY_TIMESTAMP;

        for (std::size_t i = 0; i < kPassCount; ++i)
        {
            hr = device->CreateQuery(&tsDesc, &m_beginQueries[i]);
            if (FAILED(hr)) { return false; }

            hr = device->CreateQuery(&tsDesc, &m_endQueries[i]);
            if (FAILED(hr)) { return false; }
        }

        m_initialized = true;
        return true;
    }

#endif // MITIRU_HAS_DX11

#ifdef MITIRU_HAS_OPENGL

    /// @brief OpenGLで初期化する
    /// @return 初期化成功時 true
    bool initGL()
    {
        for (std::size_t i = 0; i < kPassCount; ++i)
        {
            glGenQueries(1, &m_glBeginQueries[i]);
            glGenQueries(1, &m_glEndQueries[i]);

            if (m_glBeginQueries[i] == 0 || m_glEndQueries[i] == 0)
            {
                return false;
            }
        }

        m_initialized = true;
        m_useGL = true;
        return true;
    }

#endif // MITIRU_HAS_OPENGL

    /// @brief Nullバックエンド初期化（テスト用）
    /// @return 常に true
    bool initNull() noexcept
    {
        m_initialized = true;
        m_useNull = true;
        return true;
    }

    /// @brief リソースを解放する
    void shutdown()
    {
        if (!m_initialized)
        {
            return;
        }

#ifdef MITIRU_HAS_OPENGL
        if (m_useGL)
        {
            for (std::size_t i = 0; i < kPassCount; ++i)
            {
                if (m_glBeginQueries[i] != 0)
                {
                    glDeleteQueries(1, &m_glBeginQueries[i]);
                    m_glBeginQueries[i] = 0;
                }
                if (m_glEndQueries[i] != 0)
                {
                    glDeleteQueries(1, &m_glEndQueries[i]);
                    m_glEndQueries[i] = 0;
                }
            }
        }
#endif

#ifdef MITIRU_HAS_DX11
        m_disjointQuery.Reset();
        for (std::size_t i = 0; i < kPassCount; ++i)
        {
            m_beginQueries[i].Reset();
            m_endQueries[i].Reset();
        }
        m_d3dContext = nullptr;
#endif

        m_initialized = false;
    }

    // ── パス計測 ─────────────────────────────────────────────

    /// @brief フレーム開始時にdisjointクエリを開始する
    void beginFrame()
    {
        if (!m_initialized)
        {
            return;
        }
        m_passMs.fill(0.0f);
        m_passActive.fill(false);

#ifdef MITIRU_HAS_DX11
        if (!m_useNull && !m_useGL && m_d3dContext && m_disjointQuery)
        {
            m_d3dContext->Begin(m_disjointQuery.Get());
            m_disjointActive = true;
        }
#endif
    }

    /// @brief パス計測を開始する
    /// @param p 計測するパス
    void beginPass(Pass p)
    {
        if (!m_initialized)
        {
            return;
        }
        const auto idx = static_cast<std::size_t>(p);
        if (idx >= kPassCount)
        {
            return;
        }
        m_passActive[idx] = true;

#ifdef MITIRU_HAS_DX11
        if (!m_useNull && !m_useGL && m_d3dContext && m_beginQueries[idx])
        {
            m_d3dContext->End(m_beginQueries[idx].Get());
        }
#endif

#ifdef MITIRU_HAS_OPENGL
        if (m_useGL)
        {
            glQueryCounter(m_glBeginQueries[idx], GL_TIMESTAMP);
        }
#endif
    }

    /// @brief パス計測を終了する
    /// @param p 計測するパス
    void endPass(Pass p)
    {
        if (!m_initialized)
        {
            return;
        }
        const auto idx = static_cast<std::size_t>(p);
        if (idx >= kPassCount || !m_passActive[idx])
        {
            return;
        }

#ifdef MITIRU_HAS_DX11
        if (!m_useNull && !m_useGL && m_d3dContext && m_endQueries[idx])
        {
            m_d3dContext->End(m_endQueries[idx].Get());
        }
#endif

#ifdef MITIRU_HAS_OPENGL
        if (m_useGL)
        {
            glQueryCounter(m_glEndQueries[idx], GL_TIMESTAMP);
        }
#endif
    }

    /// @brief フレーム終了後に計測結果を収集する
    /// @details disjointクエリ終了後、各パスのタイムスタンプ差分からミリ秒を計算する。
    void resolve()
    {
        if (!m_initialized || m_useNull)
        {
            return;
        }

#ifdef MITIRU_HAS_DX11
        if (!m_useGL && m_d3dContext && m_disjointActive)
        {
            m_d3dContext->End(m_disjointQuery.Get());
            m_disjointActive = false;

            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
            while (m_d3dContext->GetData(m_disjointQuery.Get(),
                                          &disjointData,
                                          sizeof(disjointData), 0) == S_FALSE)
            {
                // GPUの結果を待つ
            }

            if (!disjointData.Disjoint)
            {
                const double ticksToMs =
                    1000.0 / static_cast<double>(disjointData.Frequency);

                for (std::size_t i = 0; i < kPassCount; ++i)
                {
                    if (!m_passActive[i])
                    {
                        continue;
                    }

                    UINT64 beginTs = 0;
                    UINT64 endTs = 0;
                    m_d3dContext->GetData(m_beginQueries[i].Get(),
                                          &beginTs, sizeof(beginTs), 0);
                    m_d3dContext->GetData(m_endQueries[i].Get(),
                                          &endTs, sizeof(endTs), 0);

                    m_passMs[i] = static_cast<float>(
                        static_cast<double>(endTs - beginTs) * ticksToMs);
                }
            }
        }
#endif

#ifdef MITIRU_HAS_OPENGL
        if (m_useGL)
        {
            for (std::size_t i = 0; i < kPassCount; ++i)
            {
                if (!m_passActive[i])
                {
                    continue;
                }

                GLuint64 beginTs = 0;
                GLuint64 endTs = 0;

                GLint available = 0;
                while (!available)
                {
                    glGetQueryObjectiv(m_glEndQueries[i],
                                       GL_QUERY_RESULT_AVAILABLE, &available);
                }

                glGetQueryObjectui64v(m_glBeginQueries[i],
                                      GL_QUERY_RESULT, &beginTs);
                glGetQueryObjectui64v(m_glEndQueries[i],
                                      GL_QUERY_RESULT, &endTs);

                // GL タイムスタンプはナノ秒単位
                m_passMs[i] = static_cast<float>(
                    static_cast<double>(endTs - beginTs) / 1000000.0);
            }
        }
#endif
    }

    // ── 結果取得 ─────────────────────────────────────────────

    /// @brief パスごとのGPU処理時間を取得する
    /// @param p パス
    /// @return ミリ秒
    [[nodiscard]] float passMs(Pass p) const noexcept
    {
        const auto idx = static_cast<std::size_t>(p);
        if (idx >= kPassCount)
        {
            return 0.0f;
        }
        return m_passMs[idx];
    }

    /// @brief GPU全体の処理時間を取得する
    /// @return ミリ秒（全パスの合計）
    [[nodiscard]] float totalGpuMs() const noexcept
    {
        float total = 0.0f;
        for (std::size_t i = 0; i < kPassCount; ++i)
        {
            total += m_passMs[i];
        }
        return total;
    }

    /// @brief 初期化済みかどうかを取得する
    [[nodiscard]] bool isInitialized() const noexcept
    {
        return m_initialized;
    }

    /// @brief パスの表示名を取得する
    /// @param p パス
    /// @return 表示名
    [[nodiscard]] static constexpr std::string_view passName(Pass p) noexcept
    {
        switch (p)
        {
        case Pass::Shadow:      return "Shadow";
        case Pass::Main3D:      return "Main3D";
        case Pass::PostProcess: return "PostProcess";
        case Pass::UI2D:        return "UI2D";
        default:                return "Unknown";
        }
    }

private:
    std::array<float, kPassCount> m_passMs{};      ///< パス別GPU時間（ミリ秒）
    std::array<bool, kPassCount> m_passActive{};   ///< パスが計測中か
    bool m_initialized = false;                     ///< 初期化済みフラグ
    bool m_useNull = false;                         ///< Nullバックエンドフラグ
    bool m_useGL = false;                           ///< OpenGLバックエンドフラグ

#ifdef MITIRU_HAS_DX11
    ID3D11DeviceContext* m_d3dContext = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Query> m_disjointQuery;
    std::array<Microsoft::WRL::ComPtr<ID3D11Query>, kPassCount> m_beginQueries;
    std::array<Microsoft::WRL::ComPtr<ID3D11Query>, kPassCount> m_endQueries;
    bool m_disjointActive = false;
#endif

#ifdef MITIRU_HAS_OPENGL
    std::array<GLuint, kPassCount> m_glBeginQueries{};
    std::array<GLuint, kPassCount> m_glEndQueries{};
#endif
};

/// @brief RAIIスコープでGPUパスを計測する
///
/// @code
/// {
///     GpuTimerScope scope(timer, GpuTimer::Pass::Main3D);
///     // ... GPU描画 ...
/// }
/// @endcode
class GpuTimerScope
{
public:
    /// @brief コンストラクタ（パス計測開始）
    /// @param timer GpuTimerインスタンス（非所有）
    /// @param pass 計測パス
    GpuTimerScope(GpuTimer& timer, GpuTimer::Pass pass) noexcept
        : m_timer(timer)
        , m_pass(pass)
    {
        m_timer.beginPass(m_pass);
    }

    /// @brief デストラクタ（パス計測終了）
    ~GpuTimerScope()
    {
        m_timer.endPass(m_pass);
    }

    /// コピー禁止
    GpuTimerScope(const GpuTimerScope&) = delete;
    GpuTimerScope& operator=(const GpuTimerScope&) = delete;

    /// ムーブ禁止
    GpuTimerScope(GpuTimerScope&&) = delete;
    GpuTimerScope& operator=(GpuTimerScope&&) = delete;

private:
    GpuTimer& m_timer;   ///< GpuTimerインスタンス
    GpuTimer::Pass m_pass; ///< 計測パス
};

} // namespace mitiru::debug
