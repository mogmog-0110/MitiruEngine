#pragma once

/// @file Migration.hpp
/// @brief SaveSchema<T>::migrations() 向けの合成可能な migration op 群。
///
/// **目的。** `MigrationChain<T>::addStep` は旧 save blob を upgrade するための
/// `std::function<Json(Json)>` を受け取る。consumer は同じ形を何度も書きがち:
/// 「欠落 field を backfill」「field を rename」「field を drop」。本ヘッダは
/// これら定番ケースを宣言的に保つための出来合い lambda factory を提供する。
/// `compose(...)` を使えば 1 step で複数の処理を順に行える。
///
/// **典型的な使い方**
/// @code
/// using mitiru::data::Migration;
///
/// SaveSchema<PlayerSave> schema(4);
/// schema.migrations()
///     .addStep(1, 2, Migration::backfillField("unlockedRecipes",
///                                              Json::array({"Cookie"})))
///     .addStep(2, 3, Migration::renameField("score", "totalScore"))
///     .addStep(3, 4, Migration::compose({
///         Migration::removeField("legacyDebug"),
///         Migration::setField("schemaTouched", true),
///     }));
/// @endcode
///
/// 全ヘルパーは引数を **値キャプチャ** する `std::function<Json(Json)>` を返す
/// ため、返された op は factory 呼び出しより安全に長生きする。
///
/// @note `MigrationChain<T>::addStep` は `*this` を返すので、上記の fluent chain
///       がそのまま動く。fluent style が可読性を損なう場合は `addStep` を
///       (戻り値を無視して) 1 行ずつ呼んでも構わない。

#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <mitiru/data/JsonBinding.hpp>

namespace mitiru::data {

/// @brief `MigrationChain::addStep` で使える合成可能な migration ヘルパー。
///
/// stateless な utility class。全メンバは出来合いの `std::function<Json(Json)>`
/// lambda を返す static factory function。キャプチャは値渡しなので、得られる op
/// は `MigrationChain` に安全に格納できる。
class Migration
{
public:
    /// @brief migration op の signature: JSON object を受け取り、upgrade 済み JSON object を返す。
    using Op = std::function<Json(Json)>;

    // -----------------------------------------------------------------------
    // 単一 field の op
    // -----------------------------------------------------------------------

    /// @brief 欠落している field を @p defaultValue で backfill する。既存なら no-op。
    /// @param field         調べる field 名。
    /// @param defaultValue  field が無い場合に設定する値。
    [[nodiscard]] static inline Op backfillField(std::string field, Json defaultValue)
    {
        return [field = std::move(field), defaultValue = std::move(defaultValue)]
               (Json data) -> Json
        {
            if (!data.contains(field))
            {
                data[field] = defaultValue;
            }
            return data;
        };
    }

    /// @brief field を rename する。@p oldName が無い場合は no-op。
    /// @param oldName  元の field 名 (成功時に erase される)。
    /// @param newName  先の field 名 (既存なら上書き)。
    [[nodiscard]] static inline Op renameField(std::string oldName, std::string newName)
    {
        return [oldName = std::move(oldName), newName = std::move(newName)]
               (Json data) -> Json
        {
            if (data.contains(oldName))
            {
                data[newName] = data[oldName];
                data.erase(oldName);
            }
            return data;
        };
    }

    /// @brief field を削除する。存在しなければ no-op。
    [[nodiscard]] static inline Op removeField(std::string field)
    {
        return [field = std::move(field)](Json data) -> Json
        {
            if (data.contains(field))
            {
                data.erase(field);
            }
            return data;
        };
    }

    /// @brief field を無条件に設定する (既存値があれば上書き)。
    [[nodiscard]] static inline Op setField(std::string field, Json value)
    {
        return [field = std::move(field), value = std::move(value)]
               (Json data) -> Json
        {
            data[field] = value;
            return data;
        };
    }

    /// @brief field の値に transform を適用する。field が無ければ no-op。
    /// @param field  値が @p fn に渡される field。
    /// @param fn     新しい値を返す純関数。
    [[nodiscard]] static inline Op transformField(std::string field,
                                                  std::function<Json(Json)> fn)
    {
        return [field = std::move(field), fn = std::move(fn)]
               (Json data) -> Json
        {
            if (data.contains(field))
            {
                data[field] = fn(data[field]);
            }
            return data;
        };
    }

    // -----------------------------------------------------------------------
    // 合成
    // -----------------------------------------------------------------------

    /// @brief 複数の op を左から右へ合成する。
    /// @details 空リストは identity op を返す。各 op は直前の op の出力を
    ///          受け取る。
    [[nodiscard]] static inline Op compose(std::initializer_list<Op> ops)
    {
        return compose(std::vector<Op>(ops.begin(), ops.end()));
    }

    /// @brief runtime に構築した op の vector で合成する。
    [[nodiscard]] static inline Op compose(std::vector<Op> ops)
    {
        return [ops = std::move(ops)](Json data) -> Json
        {
            for (const auto& op : ops)
            {
                data = op(std::move(data));
            }
            return data;
        };
    }
};

} // namespace mitiru::data
