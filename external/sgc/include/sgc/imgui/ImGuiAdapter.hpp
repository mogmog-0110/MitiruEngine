#pragma once

/// @file ImGuiAdapter.hpp
/// @brief ImGuiバックエンド抽象インターフェースとヘルパー
///
/// ImGuiに直接依存せず、デバッグUI機能を抽象化するレイヤー。
/// 実際のImGui実装はアプリケーション側で注入する。
///
/// @code
/// // バックエンド実装例
/// class OpenGLImGuiBackend : public sgc::imgui::IImGuiBackend
/// {
/// public:
///     void newFrame() override { ImGui_ImplOpenGL3_NewFrame(); }
///     void render() override { ImGui_ImplOpenGL3_RenderDrawData(...); }
///     void shutdown() override { ImGui_ImplOpenGL3_Shutdown(); }
/// };
///
/// // ヘルパー使用例
/// sgc::imgui::ImGuiHelper helper(backend);
/// helper.drawProfiler(profiler);
/// @endcode

#include <cstdint>
#include <string>
#include <vector>

namespace sgc::imgui
{

/// @brief ImGuiバックエンド抽象インターフェース
///
/// レンダリングAPI固有のImGui初期化・描画・終了処理を抽象化する。
/// OpenGL, DirectX, Vulkan等のバックエンドを差し替え可能にする。
class IImGuiBackend
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IImGuiBackend() = default;

	/// @brief 新しいフレームを開始する
	///
	/// ImGui::NewFrame()の前に呼び出すバックエンド固有の処理。
	virtual void newFrame() = 0;

	/// @brief フレームを描画する
	///
	/// ImGui::Render()の後に呼び出すバックエンド固有の描画処理。
	virtual void render() = 0;

	/// @brief バックエンドを終了する
	///
	/// リソースの解放とクリーンアップを行う。
	virtual void shutdown() = 0;

	/// @brief バックエンドが初期化済みかどうか
	/// @return 初期化済みならtrue
	[[nodiscard]]
	virtual bool isInitialized() const = 0;
};

/// @brief プロファイラー表示用のエントリ
struct ProfileEntry
{
	std::string name;       ///< プロファイル項目名
	float timeMs;           ///< 実行時間（ミリ秒）
	uint32_t callCount;     ///< 呼び出し回数
};

/// @brief エンティティインスペクター表示用の情報
struct EntityDisplayInfo
{
	uint32_t entityId;                       ///< エンティティID
	std::vector<std::string> componentNames; ///< コンポーネント名リスト
	bool isAlive;                            ///< 生存フラグ
};

/// @brief Tweakパネル表示用の項目
struct TweakDisplayItem
{
	std::string name;    ///< 項目名
	std::string value;   ///< 現在値（文字列表現）
	std::string type;    ///< 型名
};

/// @brief ImGuiユーティリティヘルパー
///
/// sgcの各種システム（ECS, Profiler, Tweak等）の
/// デバッグUI描画を支援する静的ユーティリティクラス。
/// 実際のImGui描画は行わず、表示用データの収集・整形を担う。
class ImGuiHelper
{
public:
	/// @brief エンティティインスペクター用データを収集する
	/// @tparam WorldType ECSワールド型
	/// @tparam EntityType エンティティ型
	/// @param world ECSワールド
	/// @param entity 対象エンティティ
	/// @return 表示用エンティティ情報
	///
	/// @note WorldTypeにはisAlive(entity)メソッドが必要
	template<typename WorldType, typename EntityType>
	[[nodiscard]]
	static EntityDisplayInfo collectEntityInfo(
		const WorldType& world,
		EntityType entity)
	{
		EntityDisplayInfo info;
		info.entityId = static_cast<uint32_t>(entity);
		info.isAlive = world.isAlive(entity);
		return info;
	}

	/// @brief プロファイラー用データを収集する
	/// @tparam ProfilerType プロファイラー型
	/// @param profiler プロファイラー
	/// @return プロファイルエントリのリスト
	///
	/// @note ProfilerTypeにはgetEntries()メソッドが必要。
	///       各エントリはname(), averageMs(), callCount()を持つこと。
	template<typename ProfilerType>
	[[nodiscard]]
	static std::vector<ProfileEntry> collectProfileData(
		const ProfilerType& profiler)
	{
		std::vector<ProfileEntry> result;
		for (const auto& entry : profiler.getEntries())
		{
			ProfileEntry pe;
			pe.name = entry.name();
			pe.timeMs = static_cast<float>(entry.averageMs());
			pe.callCount = static_cast<uint32_t>(entry.callCount());
			result.push_back(pe);
		}
		return result;
	}

	/// @brief Tweakパネル用データを収集する
	/// @tparam TweakRegistryType Tweakレジストリ型
	/// @param registry Tweakレジストリ
	/// @return Tweak項目リスト
	///
	/// @note TweakRegistryTypeにはgetAllItems()メソッドが必要。
	///       各項目はname(), valueAsString(), typeName()を持つこと。
	template<typename TweakRegistryType>
	[[nodiscard]]
	static std::vector<TweakDisplayItem> collectTweakData(
		const TweakRegistryType& registry)
	{
		std::vector<TweakDisplayItem> result;
		for (const auto& item : registry.getAllItems())
		{
			TweakDisplayItem ti;
			ti.name = item.name();
			ti.value = item.valueAsString();
			ti.type = item.typeName();
			result.push_back(ti);
		}
		return result;
	}
};

} // namespace sgc::imgui
