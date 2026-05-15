#pragma once

/// @file SaveLoadScreen.hpp
/// @brief ビジュアルノベル用セーブ/ロード画面
/// @details スロット一覧表示、サムネイルプレビュー、セーブデータのシリアライズを提供する。

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <mitiru/bridge/SaveBridge.hpp>
#include <mitiru/observe/JsonEscape.hpp>
#include <mitiru/ui/UITheme.hpp>
#include <mitiru/vn/ConfirmDialog.hpp>

namespace mitiru::vn
{

/// @brief セーブ/ロード画面の動作モード
enum class SaveLoadMode : std::uint8_t
{
	Save = 0,  ///< セーブモード
	Load = 1,  ///< ロードモード
};

/// @brief サムネイルの生データ
struct ThumbnailData
{
	std::vector<std::uint8_t> pixels;   ///< RGBA ピクセルデータ
	int width  = 0;                     ///< 幅（ピクセル）
	int height = 0;                     ///< 高さ（ピクセル）

	/// @brief 有効なサムネイルかどうか
	[[nodiscard]] bool isValid() const noexcept
	{
		return width > 0 && height > 0 &&
		       static_cast<int>(pixels.size()) == width * height * 4;
	}

	/// @brief Base64エンコードする
	[[nodiscard]] std::string toBase64() const
	{
		static constexpr char TABLE[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		std::string result;
		if (pixels.empty())
		{
			return result;
		}

		const std::size_t inputLen = pixels.size();
		result.reserve(((inputLen + 2) / 3) * 4);

		for (std::size_t i = 0; i < inputLen; i += 3)
		{
			const std::uint32_t b0 = pixels[i];
			const std::uint32_t b1 = (i + 1 < inputLen) ? pixels[i + 1] : 0;
			const std::uint32_t b2 = (i + 2 < inputLen) ? pixels[i + 2] : 0;
			const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

			result += TABLE[(triple >> 18) & 0x3F];
			result += TABLE[(triple >> 12) & 0x3F];
			result += (i + 1 < inputLen) ? TABLE[(triple >> 6) & 0x3F] : '=';
			result += (i + 2 < inputLen) ? TABLE[triple & 0x3F] : '=';
		}

		return result;
	}

	/// @brief Base64デコードする
	/// @param encoded Base64文字列
	/// @param w 幅
	/// @param h 高さ
	/// @return デコードされたサムネイル
	[[nodiscard]] static ThumbnailData fromBase64(std::string_view encoded,
	                                              int w, int h)
	{
		ThumbnailData result;
		result.width = w;
		result.height = h;

		if (encoded.empty())
		{
			return result;
		}

		auto decodeChar = [](char c) -> std::uint8_t
		{
			if (c >= 'A' && c <= 'Z') return static_cast<std::uint8_t>(c - 'A');
			if (c >= 'a' && c <= 'z') return static_cast<std::uint8_t>(c - 'a' + 26);
			if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0' + 52);
			if (c == '+') return 62;
			if (c == '/') return 63;
			return 0;
		};

		result.pixels.reserve(static_cast<std::size_t>(w * h * 4));

		for (std::size_t i = 0; i + 3 < encoded.size(); i += 4)
		{
			const std::uint32_t a = decodeChar(encoded[i]);
			const std::uint32_t b = decodeChar(encoded[i + 1]);
			const std::uint32_t c = decodeChar(encoded[i + 2]);
			const std::uint32_t d = decodeChar(encoded[i + 3]);
			const std::uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;

			result.pixels.push_back(static_cast<std::uint8_t>((triple >> 16) & 0xFF));
			if (encoded[i + 2] != '=')
			{
				result.pixels.push_back(static_cast<std::uint8_t>((triple >> 8) & 0xFF));
			}
			if (encoded[i + 3] != '=')
			{
				result.pixels.push_back(static_cast<std::uint8_t>(triple & 0xFF));
			}
		}

		return result;
	}
};

/// @brief スクリーンキャプチャからサムネイルを生成するヘルパー
struct ThumbnailCapture
{
	/// @brief デフォルトサムネイルサイズ
	static constexpr int DEFAULT_WIDTH  = 160;
	static constexpr int DEFAULT_HEIGHT = 90;

	/// @brief フルスクリーンのRGBA画像をダウンスケールしてサムネイルを生成する
	/// @param srcPixels ソースRGBAピクセルデータ
	/// @param srcWidth ソース幅
	/// @param srcHeight ソース高さ
	/// @param dstWidth 出力幅（デフォルト160）
	/// @param dstHeight 出力高さ（デフォルト90）
	/// @return ダウンスケールされたサムネイル
	[[nodiscard]] static ThumbnailData capture(
		const std::uint8_t* srcPixels,
		int srcWidth, int srcHeight,
		int dstWidth = DEFAULT_WIDTH,
		int dstHeight = DEFAULT_HEIGHT)
	{
		ThumbnailData thumb;
		thumb.width = dstWidth;
		thumb.height = dstHeight;
		thumb.pixels.resize(static_cast<std::size_t>(dstWidth * dstHeight * 4));

		if (!srcPixels || srcWidth <= 0 || srcHeight <= 0)
		{
			std::memset(thumb.pixels.data(), 0, thumb.pixels.size());
			return thumb;
		}

		/// 最近傍補間によるダウンスケール
		const float xScale = static_cast<float>(srcWidth) / static_cast<float>(dstWidth);
		const float yScale = static_cast<float>(srcHeight) / static_cast<float>(dstHeight);

		for (int dy = 0; dy < dstHeight; ++dy)
		{
			const int sy = std::min(static_cast<int>(static_cast<float>(dy) * yScale),
			                        srcHeight - 1);
			for (int dx = 0; dx < dstWidth; ++dx)
			{
				const int sx = std::min(static_cast<int>(static_cast<float>(dx) * xScale),
				                        srcWidth - 1);
				const std::size_t srcIdx = static_cast<std::size_t>((sy * srcWidth + sx) * 4);
				const std::size_t dstIdx = static_cast<std::size_t>((dy * dstWidth + dx) * 4);

				thumb.pixels[dstIdx + 0] = srcPixels[srcIdx + 0];
				thumb.pixels[dstIdx + 1] = srcPixels[srcIdx + 1];
				thumb.pixels[dstIdx + 2] = srcPixels[srcIdx + 2];
				thumb.pixels[dstIdx + 3] = srcPixels[srcIdx + 3];
			}
		}

		return thumb;
	}
};

/// @brief セーブスロットの情報
struct SaveSlotInfo
{
	int slotNumber       = 0;       ///< スロット番号
	std::string title;              ///< セーブタイトル（シーンタイトル等）
	std::string dateTime;           ///< セーブ日時文字列
	float playTime       = 0.0f;    ///< プレイ時間（秒）
	std::string chapterName;        ///< チャプター名
	ThumbnailData thumbnail;        ///< サムネイル画像
	bool isEmpty         = true;    ///< 空スロットかどうか

	/// @brief プレイ時間を "HH:MM:SS" 形式で取得する
	[[nodiscard]] std::string playTimeFormatted() const
	{
		const int totalSec = static_cast<int>(playTime);
		const int hours   = totalSec / 3600;
		const int minutes = (totalSec % 3600) / 60;
		const int seconds = totalSec % 60;

		std::string result;
		if (hours < 10) result += "0";
		result += std::to_string(hours) + ":";
		if (minutes < 10) result += "0";
		result += std::to_string(minutes) + ":";
		if (seconds < 10) result += "0";
		result += std::to_string(seconds);
		return result;
	}
};

/// @brief VN全体の状態をシリアライズ/デシリアライズするヘルパー
/// @details セーブデータのJSON形式を定義し、バージョン管理をサポートする。
struct SaveDataSerializer
{
	/// @brief 現在のセーブデータバージョン
	static constexpr int CURRENT_VERSION = 1;

	/// @brief VN状態をJSON文字列にシリアライズする
	/// @param scriptLabel 現在のスクリプトラベル
	/// @param lineIndex 現在の行番号
	/// @param chapterName チャプター名
	/// @param playTime プレイ時間（秒）
	/// @param flags ゲームフラグ（キー=値のペア）
	/// @param bgmId 再生中のBGM ID
	/// @param backgroundId 表示中の背景ID
	/// @param thumbnail サムネイルデータ
	/// @param readProgress 既読行数
	/// @return JSON文字列
	[[nodiscard]] static std::string serialize(
		std::string_view scriptLabel,
		int lineIndex,
		std::string_view chapterName,
		float playTime,
		const std::vector<std::pair<std::string, std::string>>& flags,
		std::string_view bgmId,
		std::string_view backgroundId,
		const ThumbnailData& thumbnail,
		int readProgress)
	{
		std::string json = "{\n";
		json += "  \"version\": " + std::to_string(CURRENT_VERSION) + ",\n";
		json += "  \"scriptLabel\": \"" + observe::jsonEscape(scriptLabel) + "\",\n";
		json += "  \"lineIndex\": " + std::to_string(lineIndex) + ",\n";
		json += "  \"chapterName\": \"" + observe::jsonEscape(chapterName) + "\",\n";
		json += "  \"playTime\": " + std::to_string(playTime) + ",\n";
		json += "  \"bgmId\": \"" + observe::jsonEscape(bgmId) + "\",\n";
		json += "  \"backgroundId\": \"" + observe::jsonEscape(backgroundId) + "\",\n";
		json += "  \"readProgress\": " + std::to_string(readProgress) + ",\n";

		/// フラグ
		json += "  \"flags\": {";
		for (std::size_t i = 0; i < flags.size(); ++i)
		{
			if (i > 0) json += ",";
			json += "\n    \"" + observe::jsonEscape(flags[i].first) + "\": \""
			     + observe::jsonEscape(flags[i].second) + "\"";
		}
		if (!flags.empty()) json += "\n  ";
		json += "},\n";

		/// サムネイル
		json += "  \"thumbnail\": {\n";
		json += "    \"width\": " + std::to_string(thumbnail.width) + ",\n";
		json += "    \"height\": " + std::to_string(thumbnail.height) + ",\n";
		json += "    \"data\": \"" + thumbnail.toBase64() + "\"\n";
		json += "  },\n";

		/// 日時
		json += "  \"savedAt\": \"" + currentDateTimeString() + "\"\n";
		json += "}";

		return json;
	}

	/// @brief JSON文字列からセーブスロット情報を復元する
	/// @param json JSON文字列
	/// @param slotNumber スロット番号
	/// @return セーブスロット情報
	[[nodiscard]] static SaveSlotInfo deserializeSlotInfo(
		std::string_view json, int slotNumber)
	{
		SaveSlotInfo info;
		info.slotNumber = slotNumber;
		info.isEmpty = json.empty();

		if (info.isEmpty)
		{
			return info;
		}

		info.chapterName = parseString(json, "chapterName");
		info.title = info.chapterName;
		info.dateTime = parseString(json, "savedAt");
		info.playTime = parseFloat(json, "playTime");

		/// サムネイル復元
		const int thumbW = parseInt(json, "width", ThumbnailCapture::DEFAULT_WIDTH);
		const int thumbH = parseInt(json, "height", ThumbnailCapture::DEFAULT_HEIGHT);
		const auto thumbData = parseString(json, "data");
		if (!thumbData.empty())
		{
			info.thumbnail = ThumbnailData::fromBase64(thumbData, thumbW, thumbH);
		}

		return info;
	}

	/// @brief セーブデータのバージョンを取得する
	/// @param json JSON文字列
	/// @return バージョン番号（パース失敗時は0）
	[[nodiscard]] static int parseVersion(std::string_view json)
	{
		return parseInt(json, "version", 0);
	}

private:
	/// @brief 現在日時の文字列を取得する
	[[nodiscard]] static std::string currentDateTimeString()
	{
		const auto now = std::chrono::system_clock::now();
		const auto time_t = std::chrono::system_clock::to_time_t(now);
		std::tm tm_buf{};
#if defined(_MSC_VER) || defined(_WIN32)
		localtime_s(&tm_buf, &time_t);
#else
		localtime_r(&time_t, &tm_buf);
#endif
		std::array<char, 32> buf{};
		std::strftime(buf.data(), buf.size(), "%Y-%m-%d %H:%M:%S", &tm_buf);
		return std::string(buf.data());
	}

	/// @brief JSON簡易パーサー: 文字列値
	[[nodiscard]] static std::string parseString(std::string_view json,
	                                             std::string_view key)
	{
		const std::string search = "\"" + std::string(key) + "\"";
		const auto keyPos = json.find(search);
		if (keyPos == std::string_view::npos)
		{
			return {};
		}
		auto colonPos = json.find(':', keyPos + search.size());
		if (colonPos == std::string_view::npos)
		{
			return {};
		}
		++colonPos;
		while (colonPos < json.size() && (json[colonPos] == ' ' || json[colonPos] == '\t'
		       || json[colonPos] == '\n' || json[colonPos] == '\r'))
		{
			++colonPos;
		}
		if (colonPos >= json.size() || json[colonPos] != '"')
		{
			return {};
		}
		const auto endQuote = json.find('"', colonPos + 1);
		if (endQuote == std::string_view::npos)
		{
			return {};
		}
		return std::string(json.substr(colonPos + 1, endQuote - colonPos - 1));
	}

	/// @brief JSON簡易パーサー: float値
	[[nodiscard]] static float parseFloat(std::string_view json,
	                                      std::string_view key)
	{
		const std::string search = "\"" + std::string(key) + "\"";
		const auto keyPos = json.find(search);
		if (keyPos == std::string_view::npos)
		{
			return 0.0f;
		}
		auto colonPos = json.find(':', keyPos + search.size());
		if (colonPos == std::string_view::npos)
		{
			return 0.0f;
		}
		++colonPos;
		while (colonPos < json.size() && (json[colonPos] == ' ' || json[colonPos] == '\t'))
		{
			++colonPos;
		}
		try
		{
			return std::stof(std::string(json.substr(colonPos)));
		}
		catch (...)
		{
			return 0.0f;
		}
	}

	/// @brief JSON簡易パーサー: int値
	[[nodiscard]] static int parseInt(std::string_view json,
	                                  std::string_view key,
	                                  int defaultVal)
	{
		const std::string search = "\"" + std::string(key) + "\"";
		const auto keyPos = json.find(search);
		if (keyPos == std::string_view::npos)
		{
			return defaultVal;
		}
		auto colonPos = json.find(':', keyPos + search.size());
		if (colonPos == std::string_view::npos)
		{
			return defaultVal;
		}
		++colonPos;
		while (colonPos < json.size() && (json[colonPos] == ' ' || json[colonPos] == '\t'))
		{
			++colonPos;
		}
		try
		{
			return std::stoi(std::string(json.substr(colonPos)));
		}
		catch (...)
		{
			return defaultVal;
		}
	}
};

/// @brief セーブ/ロード画面のアクション結果
enum class SaveLoadAction : std::uint8_t
{
	None        = 0,  ///< 何もしない
	Saved       = 1,  ///< セーブ完了
	Loaded      = 2,  ///< ロード完了
	Deleted     = 3,  ///< 削除完了
	Closed      = 4,  ///< 画面を閉じた
};

/// @brief セーブ/ロード画面のグリッドレイアウト設定
struct SaveLoadGridConfig
{
	int slotsPerRow = 4;          ///< 1行あたりのスロット数
	int rowsPerPage = 5;          ///< 1ページあたりの行数
	int totalSlots = 100;         ///< 総スロット数
	int quickSaveSlot = 0;        ///< クイックセーブ用スロット番号
	int thumbnailWidth = 160;     ///< サムネイル幅（ピクセル）
	int thumbnailHeight = 90;     ///< サムネイル高さ（ピクセル）
	float slotSpacing = 8.0f;     ///< スロット間のスペース

	/// @brief 1ページあたりのスロット数を計算する
	[[nodiscard]] int slotsPerPage() const noexcept
	{
		return slotsPerRow * rowsPerPage;
	}
};

/// @brief ビジュアルノベル用セーブ/ロード画面
/// @details スロット一覧のグリッド/リスト表示、ページネーション、
///          確認ダイアログ付きの上書き/ロード/削除操作を提供する。
///
/// @code
/// mitiru::vn::SaveLoadScreen screen;
/// screen.setSaveBridge(&saveBridge);
/// screen.open(mitiru::vn::SaveLoadMode::Save);
///
/// // セーブ実行時のコールバック
/// screen.setOnSaveRequested([&](int slot) {
///     auto json = mitiru::vn::SaveDataSerializer::serialize(...);
///     saveBridge.save(slot, json);
///     screen.refreshSlots();
/// });
///
/// // 毎フレーム
/// auto action = screen.update(dt);
/// @endcode
class SaveLoadScreen
{
public:
	/// @brief コンストラクタ
	SaveLoadScreen() noexcept = default;

	/// @brief テーマ指定付きコンストラクタ
	explicit SaveLoadScreen(const ui::UITheme& theme) noexcept
		: m_theme(theme), m_confirmDialog(theme)
	{
	}

	// ── グリッド設定 ──────────────────────────────────────

	/// @brief グリッドレイアウト設定を変更する
	/// @param config 新しいグリッド設定
	void setGridConfig(const SaveLoadGridConfig& config) noexcept
	{
		m_gridConfig = config;
	}

	/// @brief 現在のグリッドレイアウト設定を取得する
	[[nodiscard]] const SaveLoadGridConfig& gridConfig() const noexcept
	{
		return m_gridConfig;
	}

	// ── 画面制御 ──────────────────────────────────────────

	/// @brief セーブ/ロード画面を開く
	/// @param mode セーブモードまたはロードモード
	void open(SaveLoadMode mode)
	{
		m_mode = mode;
		m_visible = true;
		m_currentPage = 0;
		m_selectedSlot = -1;
		m_pendingAction = SaveLoadAction::None;
		refreshSlots();
	}

	/// @brief 画面を閉じる
	void close() noexcept
	{
		m_visible = false;
		m_selectedSlot = -1;
	}

	/// @brief 表示中かどうか
	[[nodiscard]] bool isVisible() const noexcept { return m_visible; }

	/// @brief 現在のモードを取得する
	[[nodiscard]] SaveLoadMode mode() const noexcept { return m_mode; }

	// ── SaveBridge連携 ──────────────────────────────────────

	/// @brief SaveBridgeを設定する
	/// @param bridge SaveBridgeへのポインタ（非所有）
	void setSaveBridge(bridge::SaveBridge* bridge) noexcept
	{
		m_saveBridge = bridge;
	}

	/// @brief スロット情報を再読み込みする
	void refreshSlots()
	{
		m_slots.clear();

		if (!m_saveBridge)
		{
			/// SaveBridge未設定時は空スロットで埋める
			for (int i = 1; i <= m_gridConfig.totalSlots; ++i)
			{
				SaveSlotInfo info;
				info.slotNumber = i;
				info.isEmpty = true;
				m_slots.push_back(std::move(info));
			}
			return;
		}

		const auto existingSlots = m_saveBridge->listSlots();

		for (int i = 1; i <= m_gridConfig.totalSlots; ++i)
		{
			const bool exists = std::find(existingSlots.begin(),
			                              existingSlots.end(), i)
			                    != existingSlots.end();
			if (exists)
			{
				const auto json = m_saveBridge->load(i);
				auto info = SaveDataSerializer::deserializeSlotInfo(json, i);
				m_slots.push_back(std::move(info));
			}
			else
			{
				SaveSlotInfo info;
				info.slotNumber = i;
				info.isEmpty = true;
				m_slots.push_back(std::move(info));
			}
		}
	}

	// ── ページ操作 ──────────────────────────────────────────

	/// @brief 現在のページ番号を取得する（0始まり）
	[[nodiscard]] int currentPage() const noexcept { return m_currentPage; }

	/// @brief 総ページ数を取得する
	[[nodiscard]] int totalPages() const noexcept
	{
		return (m_gridConfig.totalSlots + m_gridConfig.slotsPerPage() - 1) / m_gridConfig.slotsPerPage();
	}

	/// @brief 次のページへ移動する
	void nextPage() noexcept
	{
		if (m_currentPage < totalPages() - 1)
		{
			++m_currentPage;
			m_selectedSlot = -1;
		}
	}

	/// @brief 前のページへ移動する
	void prevPage() noexcept
	{
		if (m_currentPage > 0)
		{
			--m_currentPage;
			m_selectedSlot = -1;
		}
	}

	/// @brief 現在のページに表示するスロット一覧を取得する
	[[nodiscard]] std::vector<const SaveSlotInfo*> currentPageSlots() const
	{
		std::vector<const SaveSlotInfo*> result;
		const int startIdx = m_currentPage * m_gridConfig.slotsPerPage();
		const int endIdx = std::min(startIdx + m_gridConfig.slotsPerPage(),
		                            static_cast<int>(m_slots.size()));
		for (int i = startIdx; i < endIdx; ++i)
		{
			result.push_back(&m_slots[static_cast<std::size_t>(i)]);
		}
		return result;
	}

	// ── スロット選択 ──────────────────────────────────────

	/// @brief スロットを選択する
	/// @param slotNumber スロット番号（1始まり）
	void selectSlot(int slotNumber) noexcept
	{
		m_selectedSlot = slotNumber;
	}

	/// @brief 選択中のスロット番号を取得する（-1=未選択）
	[[nodiscard]] int selectedSlot() const noexcept { return m_selectedSlot; }

	/// @brief 選択中のスロットでアクションを実行する
	/// @details セーブモード: 空スロット→即セーブ、既存→上書き確認
	///          ロードモード: データあり→ロード確認、空→何もしない
	void executeAction()
	{
		if (m_selectedSlot < 1 || m_selectedSlot > m_gridConfig.totalSlots)
		{
			return;
		}

		const auto& slot = findSlot(m_selectedSlot);

		if (m_mode == SaveLoadMode::Save)
		{
			if (slot.isEmpty)
			{
				/// 空スロットへの即セーブ
				performSave(m_selectedSlot);
			}
			else
			{
				/// 上書き確認ダイアログ
				DialogParams params;
				params.title = "Overwrite Save";
				params.message = "Slot " + std::to_string(m_selectedSlot)
				               + " already has data. Overwrite?";
				params.buttons = DialogButtons::YesNo;
				m_pendingAction = SaveLoadAction::Saved;
				m_pendingSlot = m_selectedSlot;
				m_confirmDialog.setOnResult([this](DialogResult r)
				{
					if (r == DialogResult::Ok)
					{
						performSave(m_pendingSlot);
					}
					m_pendingAction = SaveLoadAction::None;
				});
				m_confirmDialog.show(std::move(params));
			}
		}
		else
		{
			if (slot.isEmpty)
			{
				return;
			}

			/// ロード確認ダイアログ
			DialogParams params;
			params.title = "Load Save";
			params.message = "Load from slot " + std::to_string(m_selectedSlot)
			               + "? Unsaved progress will be lost.";
			params.buttons = DialogButtons::YesNo;
			m_pendingAction = SaveLoadAction::Loaded;
			m_pendingSlot = m_selectedSlot;
			m_confirmDialog.setOnResult([this](DialogResult r)
			{
				if (r == DialogResult::Ok)
				{
					performLoad(m_pendingSlot);
				}
				m_pendingAction = SaveLoadAction::None;
			});
			m_confirmDialog.show(std::move(params));
		}
	}

	/// @brief 選択中のスロットを削除する
	void deleteSelectedSlot()
	{
		if (m_selectedSlot < 1 || m_selectedSlot > m_gridConfig.totalSlots)
		{
			return;
		}

		const auto& slot = findSlot(m_selectedSlot);
		if (slot.isEmpty)
		{
			return;
		}

		DialogParams params;
		params.title = "Delete Save";
		params.message = "Delete save in slot " + std::to_string(m_selectedSlot) + "?";
		params.buttons = DialogButtons::YesNo;
		m_pendingSlot = m_selectedSlot;
		m_confirmDialog.setOnResult([this](DialogResult r)
		{
			if (r == DialogResult::Ok)
			{
				performDelete(m_pendingSlot);
			}
		});
		m_confirmDialog.show(std::move(params));
	}

	// ── クイックセーブ/ロード ──────────────────────────────

	/// @brief クイックセーブを実行する（UIなし）
	void quickSave()
	{
		performSave(m_gridConfig.quickSaveSlot);
	}

	/// @brief クイックロードを実行する（UIなし）
	/// @return ロード成功した場合はtrue
	[[nodiscard]] bool quickLoad()
	{
		if (!m_saveBridge || !m_saveBridge->exists(m_gridConfig.quickSaveSlot))
		{
			return false;
		}
		performLoad(m_gridConfig.quickSaveSlot);
		return true;
	}

	// ── コールバック ──────────────────────────────────────

	/// @brief セーブ要求コールバックを設定する
	/// @param fn スロット番号を引数に取るコールバック
	void setOnSaveRequested(std::function<void(int)> fn)
	{
		m_onSaveRequested = std::move(fn);
	}

	/// @brief ロード要求コールバックを設定する
	/// @param fn スロット番号を引数に取るコールバック
	void setOnLoadRequested(std::function<void(int)> fn)
	{
		m_onLoadRequested = std::move(fn);
	}

	/// @brief 削除完了コールバックを設定する
	/// @param fn スロット番号を引数に取るコールバック
	void setOnDeleteCompleted(std::function<void(int)> fn)
	{
		m_onDeleteCompleted = std::move(fn);
	}

	// ── 更新 ────────────────────────────────────────────────

	/// @brief 毎フレーム更新
	/// @param deltaTime 前フレームからの経過時間（秒）
	/// @return このフレームで完了したアクション
	[[nodiscard]] SaveLoadAction update(float deltaTime)
	{
		m_confirmDialog.update(deltaTime);

		if (!m_lastCompletedAction.empty())
		{
			const auto action = m_lastCompletedAction.back();
			m_lastCompletedAction.clear();
			return action;
		}

		return SaveLoadAction::None;
	}

	// ── 確認ダイアログアクセス ──────────────────────────────

	/// @brief 確認ダイアログを取得する
	[[nodiscard]] ConfirmDialog& confirmDialog() noexcept { return m_confirmDialog; }

	/// @brief 確認ダイアログを取得する（const版）
	[[nodiscard]] const ConfirmDialog& confirmDialog() const noexcept
	{
		return m_confirmDialog;
	}

	/// @brief モーダルダイアログが表示中かどうか
	[[nodiscard]] bool isDialogActive() const noexcept
	{
		return m_confirmDialog.isVisible();
	}

	// ── 状態クエリ ──────────────────────────────────────────

	/// @brief 全スロット情報を取得する
	[[nodiscard]] const std::vector<SaveSlotInfo>& slots() const noexcept
	{
		return m_slots;
	}

	/// @brief テーマを取得する
	[[nodiscard]] const ui::UITheme& theme() const noexcept { return m_theme; }

	/// @brief テーマを設定する
	void setTheme(const ui::UITheme& theme) noexcept
	{
		m_theme = theme;
		m_confirmDialog.setTheme(theme);
	}

private:
	/// @brief スロット番号からスロット情報を検索する
	[[nodiscard]] const SaveSlotInfo& findSlot(int slotNumber) const
	{
		const auto idx = static_cast<std::size_t>(slotNumber - 1);
		if (idx < m_slots.size())
		{
			return m_slots[idx];
		}
		static const SaveSlotInfo empty{};
		return empty;
	}

	/// @brief セーブを実行する
	void performSave(int slotNumber)
	{
		if (m_onSaveRequested)
		{
			m_onSaveRequested(slotNumber);
		}
		refreshSlots();
		m_lastCompletedAction.push_back(SaveLoadAction::Saved);
	}

	/// @brief ロードを実行する
	void performLoad(int slotNumber)
	{
		if (m_onLoadRequested)
		{
			m_onLoadRequested(slotNumber);
		}
		m_lastCompletedAction.push_back(SaveLoadAction::Loaded);
	}

	/// @brief 削除を実行する
	void performDelete(int slotNumber)
	{
		if (m_saveBridge)
		{
			m_saveBridge->deleteSave(slotNumber);
		}
		if (m_onDeleteCompleted)
		{
			m_onDeleteCompleted(slotNumber);
		}
		refreshSlots();
		m_lastCompletedAction.push_back(SaveLoadAction::Deleted);
	}

	SaveLoadGridConfig m_gridConfig;                       ///< グリッドレイアウト設定
	SaveLoadMode m_mode = SaveLoadMode::Save;          ///< 動作モード
	bool m_visible = false;                            ///< 表示中フラグ
	int m_currentPage = 0;                             ///< 現在のページ番号
	int m_selectedSlot = -1;                           ///< 選択中のスロット番号
	int m_pendingSlot = -1;                            ///< 確認待ちスロット番号
	SaveLoadAction m_pendingAction = SaveLoadAction::None;  ///< 確認待ちアクション

	std::vector<SaveSlotInfo> m_slots;                 ///< スロット情報一覧
	std::vector<SaveLoadAction> m_lastCompletedAction; ///< 完了済みアクション

	bridge::SaveBridge* m_saveBridge = nullptr;        ///< SaveBridge（非所有）
	ConfirmDialog m_confirmDialog;                     ///< 確認ダイアログ
	ui::UITheme m_theme;                               ///< UIテーマ

	std::function<void(int)> m_onSaveRequested;        ///< セーブ要求コールバック
	std::function<void(int)> m_onLoadRequested;        ///< ロード要求コールバック
	std::function<void(int)> m_onDeleteCompleted;      ///< 削除完了コールバック
};

} // namespace mitiru::vn
