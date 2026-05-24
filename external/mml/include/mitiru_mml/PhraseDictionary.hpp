#pragma once
/// @file PhraseDictionary.hpp
/// @brief 音楽フレーズ辞書 — ジャンル・気分別の旋律断片コレクション
/// @details 人間が「良い」と認めた4-8音の旋律断片を収録。
///          AiComposerがこれらを組み合わせ・変奏して楽曲を構築する。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief 旋律フレーズ（スケール度数ベース）
struct Phrase
{
	std::string id;                ///< フレーズID
	std::string mood;              ///< 気分タグ ("bright", "dark", "heroic", etc.)
	std::string function;          ///< 機能タグ ("opening", "climax", "closing", "bridge")
	std::vector<int> degrees;      ///< スケール度数列（1=ルート, 2=2度, ...）
	std::vector<int> lengths;      ///< 音長列（4=四分, 8=八分, 16=十六分, 2=二分）
	std::vector<int> velocities;   ///< 音量列（8-15、省略時は全て12）
};

/// @brief フレーズ辞書
class PhraseDictionary
{
public:
	/// @brief 全フレーズを取得する
	[[nodiscard]] static const std::vector<Phrase>& all()
	{
		static const std::vector<Phrase> phrases = buildDictionary();
		return phrases;
	}

	/// @brief 気分で絞り込む
	[[nodiscard]] static std::vector<const Phrase*> byMood(std::string_view mood)
	{
		std::vector<const Phrase*> result;
		for (const auto& p : all())
		{
			if (p.mood == mood) result.push_back(&p);
		}
		return result;
	}

	/// @brief 機能で絞り込む
	[[nodiscard]] static std::vector<const Phrase*> byFunction(std::string_view function)
	{
		std::vector<const Phrase*> result;
		for (const auto& p : all())
		{
			if (p.function == function) result.push_back(&p);
		}
		return result;
	}

	/// @brief 気分+機能で絞り込む
	[[nodiscard]] static std::vector<const Phrase*> query(
		std::string_view mood, std::string_view function)
	{
		std::vector<const Phrase*> result;
		for (const auto& p : all())
		{
			if (p.mood == mood && p.function == function) result.push_back(&p);
		}
		if (result.empty())
		{
			// フォールバック: 気分のみで検索する
			return byMood(mood);
		}
		return result;
	}

	/// @brief フレーズ数
	[[nodiscard]] static int count() { return static_cast<int>(all().size()); }

private:
	static std::vector<Phrase> buildDictionary()
	{
		std::vector<Phrase> p;

		// ===============================================
		// BRIGHT / 明るい
		// ===============================================

		// 明るいオープニングフレーズ
		p.push_back({"br_op1", "bright", "opening",
			{1, 3, 5, 8, 7, 5, 3, 5},
			{4, 8, 8, 4, 8, 8, 4, 4},
			{10, 11, 12, 13, 12, 11, 10, 11}});

		p.push_back({"br_op2", "bright", "opening",
			{5, 6, 8, 6, 5, 3, 4, 5},
			{8, 8, 4, 8, 8, 8, 8, 4},
			{12, 12, 13, 12, 11, 10, 11, 12}});

		p.push_back({"br_op3", "bright", "opening",
			{1, 2, 3, 5, 3, 2, 1, 3},
			{4, 8, 8, 2, 4, 8, 8, 2},
			{11, 12, 12, 13, 12, 11, 10, 12}});

		// 明るいメインフレーズ
		p.push_back({"br_main1", "bright", "main",
			{3, 4, 5, 4, 3, 2, 1, 2, 3},
			{8, 8, 4, 8, 8, 8, 8, 8, 4},
			{12, 12, 13, 12, 11, 10, 10, 11, 12}});

		p.push_back({"br_main2", "bright", "main",
			{5, 4, 3, 5, 4, 3, 2, 1},
			{4, 8, 8, 4, 8, 8, 4, 4},
			{13, 12, 11, 13, 12, 11, 10, 10}});

		p.push_back({"br_main3", "bright", "main",
			{1, 3, 2, 4, 3, 5, 4, 3},
			{8, 8, 8, 8, 8, 4, 8, 4},
			{11, 12, 11, 12, 12, 13, 12, 11}});

		// 明るいクライマックス
		p.push_back({"br_clm1", "bright", "climax",
			{5, 6, 7, 8, 8, 7, 6, 5},
			{8, 8, 8, 2, 8, 8, 8, 2},
			{13, 13, 14, 15, 14, 13, 12, 12}});

		p.push_back({"br_clm2", "bright", "climax",
			{8, 7, 8, 6, 7, 5, 6, 8},
			{4, 8, 4, 8, 4, 8, 4, 2},
			{14, 13, 14, 12, 13, 12, 13, 15}});

		// 明るい終止
		p.push_back({"br_cls1", "bright", "closing",
			{3, 2, 3, 4, 3, 2, 1, 1},
			{8, 8, 8, 8, 4, 8, 4, 2},
			{12, 11, 12, 12, 11, 10, 10, 10}});

		p.push_back({"br_cls2", "bright", "closing",
			{5, 4, 3, 2, 3, 2, 1, 1},
			{8, 8, 8, 8, 4, 8, 2, 2},
			{12, 11, 10, 10, 11, 10, 10, 9}});

		// 明るいブリッジ
		p.push_back({"br_brg1", "bright", "bridge",
			{4, 5, 6, 5, 4, 6, 5, 4},
			{8, 8, 4, 8, 8, 4, 4, 4},
			{11, 12, 13, 12, 11, 13, 12, 11}});

		// ===============================================
		// DARK / 暗い・哀しい
		// ===============================================

		p.push_back({"dk_op1", "dark", "opening",
			{1, 3, 2, 1, 7, 1, 2, 3},
			{4, 8, 8, 4, 8, 8, 4, 2},
			{10, 11, 10, 9, 10, 10, 11, 11}});

		p.push_back({"dk_op2", "dark", "opening",
			{5, 4, 3, 2, 1, 2, 3, 1},
			{4, 8, 8, 8, 4, 8, 4, 2},
			{11, 10, 10, 9, 9, 10, 11, 10}});

		p.push_back({"dk_main1", "dark", "main",
			{1, 2, 3, 4, 3, 2, 1, 7},
			{8, 8, 4, 4, 8, 8, 4, 4},
			{10, 11, 12, 12, 11, 10, 10, 9}});

		p.push_back({"dk_main2", "dark", "main",
			{3, 4, 5, 3, 2, 1, 7, 1},
			{4, 8, 4, 8, 8, 4, 8, 2},
			{11, 12, 13, 11, 10, 10, 9, 10}});

		p.push_back({"dk_main3", "dark", "main",
			{5, 4, 3, 5, 4, 2, 1, 2},
			{8, 8, 8, 4, 8, 8, 4, 4},
			{12, 11, 10, 12, 11, 10, 9, 10}});

		p.push_back({"dk_clm1", "dark", "climax",
			{1, 3, 5, 6, 5, 3, 2, 1},
			{8, 8, 4, 4, 8, 8, 4, 2},
			{12, 13, 14, 15, 14, 13, 11, 10}});

		p.push_back({"dk_cls1", "dark", "closing",
			{3, 2, 1, 7, 1, 7, 1, 1},
			{8, 8, 4, 8, 4, 8, 4, 2},
			{11, 10, 10, 9, 10, 9, 9, 8}});

		p.push_back({"dk_brg1", "dark", "bridge",
			{6, 5, 6, 7, 6, 5, 4, 5},
			{8, 8, 4, 4, 8, 8, 4, 4},
			{11, 10, 11, 12, 11, 10, 10, 11}});

		// ===============================================
		// HEROIC / 英雄的・勇壮
		// ===============================================

		p.push_back({"hr_op1", "heroic", "opening",
			{1, 5, 4, 3, 5, 8, 7, 5},
			{4, 4, 8, 8, 4, 4, 8, 4},
			{13, 14, 13, 12, 13, 15, 14, 13}});

		p.push_back({"hr_op2", "heroic", "opening",
			{1, 3, 5, 1, 3, 5, 8, 5},
			{8, 8, 4, 8, 8, 4, 2, 4},
			{12, 13, 14, 12, 13, 14, 15, 13}});

		p.push_back({"hr_main1", "heroic", "main",
			{8, 7, 5, 8, 7, 5, 3, 5},
			{4, 8, 8, 4, 8, 8, 4, 2},
			{14, 13, 12, 14, 13, 12, 11, 12}});

		p.push_back({"hr_main2", "heroic", "main",
			{5, 6, 8, 6, 5, 3, 4, 5},
			{8, 8, 2, 8, 8, 8, 8, 2},
			{13, 13, 15, 13, 12, 11, 12, 13}});

		p.push_back({"hr_clm1", "heroic", "climax",
			{8, 8, 7, 8, 9, 8, 7, 8},
			{8, 8, 8, 4, 4, 8, 8, 2},
			{14, 14, 13, 14, 15, 14, 13, 15}});

		p.push_back({"hr_cls1", "heroic", "closing",
			{5, 4, 3, 5, 3, 2, 1, 1},
			{4, 8, 8, 4, 8, 8, 2, 2},
			{13, 12, 11, 12, 11, 10, 10, 10}});

		// ===============================================
		// GENTLE / 優しい・穏やか
		// ===============================================

		p.push_back({"gt_op1", "gentle", "opening",
			{1, 2, 3, 2, 1, 3, 5, 3},
			{4, 4, 4, 4, 4, 4, 2, 4},
			{9, 10, 10, 10, 9, 10, 11, 10}});

		p.push_back({"gt_op2", "gentle", "opening",
			{3, 5, 4, 3, 2, 3, 4, 3},
			{4, 4, 8, 8, 4, 4, 4, 2},
			{10, 11, 10, 10, 9, 10, 10, 10}});

		p.push_back({"gt_main1", "gentle", "main",
			{5, 4, 3, 4, 5, 3, 2, 3},
			{4, 8, 8, 4, 4, 8, 8, 2},
			{11, 10, 10, 10, 11, 10, 9, 10}});

		p.push_back({"gt_main2", "gentle", "main",
			{1, 2, 3, 5, 4, 3, 2, 1},
			{4, 4, 4, 2, 4, 4, 4, 2},
			{10, 10, 11, 12, 11, 10, 10, 9}});

		p.push_back({"gt_clm1", "gentle", "climax",
			{5, 6, 7, 8, 7, 6, 5, 3},
			{4, 4, 4, 2, 4, 4, 4, 2},
			{11, 12, 12, 13, 12, 11, 11, 10}});

		p.push_back({"gt_cls1", "gentle", "closing",
			{3, 2, 1, 3, 2, 1, 2, 1},
			{4, 4, 4, 4, 4, 2, 4, 2},
			{10, 10, 9, 10, 9, 9, 9, 8}});

		// ===============================================
		// TENSE / 緊張・サスペンス
		// ===============================================

		p.push_back({"tn_op1", "tense", "opening",
			{1, 2, 1, 3, 1, 4, 1, 5},
			{8, 8, 8, 8, 8, 8, 8, 4},
			{10, 11, 10, 12, 10, 13, 10, 14}});

		p.push_back({"tn_main1", "tense", "main",
			{1, 1, 2, 1, 1, 3, 1, 1},
			{16, 16, 8, 4, 16, 16, 8, 4},
			{12, 10, 13, 12, 12, 10, 14, 12}});

		p.push_back({"tn_main2", "tense", "main",
			{5, 4, 5, 6, 5, 4, 3, 4},
			{8, 8, 8, 4, 8, 8, 8, 4},
			{12, 11, 12, 13, 12, 11, 10, 11}});

		p.push_back({"tn_clm1", "tense", "climax",
			{1, 3, 5, 7, 6, 4, 2, 1},
			{16, 16, 16, 4, 8, 8, 8, 4},
			{13, 14, 14, 15, 14, 13, 12, 11}});

		p.push_back({"tn_cls1", "tense", "closing",
			{2, 1, 2, 1, 7, 1, 7, 1},
			{8, 8, 8, 8, 8, 4, 8, 2},
			{11, 10, 11, 10, 9, 10, 9, 8}});

		// ===============================================
		// EPIC / 壮大・ドラマチック
		// ===============================================

		p.push_back({"ep_op1", "epic", "opening",
			{1, 5, 8, 5, 1, 5, 8, 10},
			{4, 4, 2, 4, 4, 4, 4, 2},
			{12, 13, 14, 13, 12, 13, 14, 15}});

		p.push_back({"ep_main1", "epic", "main",
			{8, 7, 8, 9, 8, 6, 5, 6},
			{4, 8, 8, 2, 4, 8, 8, 2},
			{14, 13, 14, 15, 14, 12, 12, 13}});

		p.push_back({"ep_main2", "epic", "main",
			{5, 6, 8, 10, 8, 6, 5, 3},
			{8, 8, 4, 2, 4, 8, 8, 2},
			{13, 13, 14, 15, 14, 13, 12, 12}});

		p.push_back({"ep_clm1", "epic", "climax",
			{8, 9, 10, 11, 10, 8, 6, 8},
			{4, 8, 8, 2, 4, 8, 8, 2},
			{14, 14, 15, 15, 14, 13, 12, 14}});

		p.push_back({"ep_cls1", "epic", "closing",
			{5, 4, 3, 5, 3, 2, 1, 1},
			{4, 4, 4, 4, 4, 4, 2, 2},
			{13, 12, 11, 12, 11, 10, 10, 9}});

		// ===============================================
		// MYSTERIOUS / 神秘的
		// ===============================================

		p.push_back({"my_op1", "mysterious", "opening",
			{1, 3, 5, 3, 1, 5, 3, 1},
			{4, 4, 2, 4, 4, 2, 4, 2},
			{9, 10, 11, 10, 9, 11, 10, 9}});

		p.push_back({"my_main1", "mysterious", "main",
			{1, 4, 3, 6, 5, 2, 1, 4},
			{4, 4, 4, 4, 4, 4, 4, 2},
			{10, 11, 10, 12, 11, 10, 9, 11}});

		p.push_back({"my_clm1", "mysterious", "climax",
			{5, 6, 5, 7, 6, 5, 4, 5},
			{4, 4, 4, 4, 4, 4, 4, 2},
			{11, 12, 11, 13, 12, 11, 10, 11}});

		p.push_back({"my_cls1", "mysterious", "closing",
			{3, 2, 3, 1, 3, 2, 1, 1},
			{4, 4, 4, 2, 4, 4, 4, 2},
			{10, 9, 10, 9, 10, 9, 8, 8}});

		return p;
	}
};

} // namespace mitiru_mml
