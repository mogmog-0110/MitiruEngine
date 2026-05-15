/// @file cpp_data_driven_minimal/main.cpp
/// @brief Headless example combining the data-driven gameplay primitives.
///
/// Covered APIs:
///   - ContentLoader<T>     -- load typed JSON content
///   - SaveSchema<T>        -- versioned save with MigrationChain
///   - BridgeInputAdapter   -- JS UI signal -> InputMapper action
///   - BridgeViewPush       -- C++ -> view state push (printf stub sink)
///   - SchemaValidator      -- ContentLoader<T>::loadStringValidated
///                             (positive + negative cases)
///   - SchemaImporter       -- draft-07 JSON Schema -> Schema (Stage 6)
///
/// Build:
///   cmake --preset default
///   cmake --build build --config Debug --target mitiru_cpp_data_driven_minimal
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "mitiru/bridge/BridgeViewPush.hpp"
#include "mitiru/data/ContentLoader.hpp"
#include "mitiru/data/JsonBinding.hpp"
#include "mitiru/data/SaveSchema.hpp"
#include "mitiru/data/SchemaImporter.hpp"
#include "mitiru/data/SchemaValidator.hpp"
#include "mitiru/input/BridgeActionRouter.hpp"
#include "mitiru/input/BridgeInputAdapter.hpp"
#include "mitiru/input/InputMapper.hpp"

// ---------------------------------------------------------------------------
// Content schema (Stage 1) -- a tiny recipe book loaded from JSON.
// ---------------------------------------------------------------------------

namespace demo {

struct Recipe {
    std::string name;
    float       bakeSeconds = 0.0f;
    int         score       = 0;
};

struct RecipeBook {
    std::vector<Recipe> recipes;
};

// NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE must sit in the SAME namespace as the
// type so ADL can find the generated to_json/from_json overloads (the
// INTRUSIVE form would trigger MSVC C2255 friend errors at namespace scope).
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Recipe, name, bakeSeconds, score)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RecipeBook, recipes)

} // namespace demo

// ---------------------------------------------------------------------------
// Save-data schema (Stage 2) -- versioned, with a v1 -> v2 migration.
// ---------------------------------------------------------------------------

namespace demo {

struct GameProgress {
    int                      score          = 0;
    std::string              currentRecipe;
    std::vector<std::string> unlockedRecipes;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
    GameProgress, score, currentRecipe, unlockedRecipes)

} // namespace demo

// ---------------------------------------------------------------------------
// Aliases (match the cpp_gameplay_minimal style)
// ---------------------------------------------------------------------------

using ActionRtr = mitiru::input::BridgeActionRouter;
using ViewPush  = mitiru::bridge::BridgeViewPush;
using Adapter   = mitiru::input::BridgeInputAdapter;
using Mapper    = mitiru::InputMapper;

template <typename T>
using SaveSchema = mitiru::data::SaveSchema<T>;
template <typename T>
using ContentLoader = mitiru::data::ContentLoader<T>;

// ---------------------------------------------------------------------------
// Stage 1: ContentLoader<RecipeBook>::loadString
// ---------------------------------------------------------------------------

static demo::RecipeBook stageOneLoadContent()
{
    std::printf("\n=== Stage 1: ContentLoader ===\n");

    // A tiny content blob -- in a real game this would live on disk under
    // assets/content/recipes.json and be loaded via ContentLoader::loadFile.
    static constexpr const char* kRecipesJson = R"({
        "recipes": [
            { "name": "Cookie",  "bakeSeconds": 2.0, "score": 10 },
            { "name": "Crepe",   "bakeSeconds": 3.5, "score": 20 },
            { "name": "Souffle", "bakeSeconds": 5.0, "score": 40 }
        ]
    })";

    auto result = ContentLoader<demo::RecipeBook>::loadString(kRecipesJson);
    if (!result.ok()) {
        std::printf("[Content] load FAILED: %s\n", result.error.c_str());
        return demo::RecipeBook{};
    }

    const demo::RecipeBook& book = *result.value;
    std::printf("[Content] loaded %zu recipes:\n", book.recipes.size());
    for (const auto& r : book.recipes) {
        std::printf("[Content]   %-8s bake=%.1fs score=%d\n",
                    r.name.c_str(), r.bakeSeconds, r.score);
    }
    return book;
}

// ---------------------------------------------------------------------------
// Stage 2: SaveSchema<GameProgress> with v1 -> v2 migration round-trip
// ---------------------------------------------------------------------------

static SaveSchema<demo::GameProgress> buildSaveSchema()
{
    // currentVersion = 2; v1 blobs had no unlockedRecipes field.
    SaveSchema<demo::GameProgress> schema(/*currentVersion=*/2);
    schema.migrations().addStep(1, 2,
        [](mitiru::data::Json data) {
            // Backfill the new field with a sensible default.
            if (!data.contains("unlockedRecipes")) {
                data["unlockedRecipes"] = mitiru::data::Json::array({ "Cookie" });
            }
            return data;
        });
    return schema;
}

static demo::GameProgress stageTwoSaveRoundTrip()
{
    std::printf("\n=== Stage 2: SaveSchema round-trip ===\n");

    SaveSchema<demo::GameProgress> schema = buildSaveSchema();

    demo::GameProgress progress{};
    progress.score           = 0;
    progress.currentRecipe   = "Cookie";
    progress.unlockedRecipes = { "Cookie" };

    const std::string blob = schema.toJsonString(progress);
    std::printf("[Save] serialized v%d blob: %s\n",
                schema.currentVersion(), blob.c_str());

    auto roundTrip = schema.fromJsonString(blob);
    if (!roundTrip.ok()) {
        std::printf("[Save] round-trip FAILED: %s\n", roundTrip.error.c_str());
        return progress;
    }
    const demo::GameProgress& restored = *roundTrip.value;
    const bool equal = (restored.score == progress.score)
        && (restored.currentRecipe == progress.currentRecipe)
        && (restored.unlockedRecipes == progress.unlockedRecipes);
    std::printf("[Save] round-trip equal=%s (score=%d recipe=%s)\n",
                equal ? "true" : "false",
                restored.score, restored.currentRecipe.c_str());

    // Demonstrate the migration path with a synthesized v1 blob.
    const std::string legacyV1 =
        R"({"version":1,"data":{"score":7,"currentRecipe":"Crepe"}})";
    auto migrated = schema.fromJsonString(legacyV1);
    if (migrated.ok()) {
        const auto& m = *migrated.value;
        std::printf("[Save] migrated v1 -> v2: score=%d recipe=%s unlocked=%zu\n",
                    m.score, m.currentRecipe.c_str(), m.unlockedRecipes.size());
    } else {
        std::printf("[Save] migration FAILED: %s\n", migrated.error.c_str());
    }

    return restored;
}

// ---------------------------------------------------------------------------
// Stage 3: BridgeInputAdapter -- JS signal -> InputMapper action
// ---------------------------------------------------------------------------

static void stageThreeBridgeInput(ActionRtr& actions, Mapper& mapper)
{
    std::printf("\n=== Stage 3: BridgeInputAdapter ===\n");

    Adapter adapter(actions, mapper);
    adapter.mapSignalToAction("ui.button|fire", "Fire");

    // Fake CEF bridge signal arriving from JS.
    actions.dispatch("ui.button|fire", "");

    const bool firePressed = mapper.isActionPressed("Fire");
    std::printf("[Input] after dispatch: isActionPressed(Fire) = %s\n",
                firePressed ? "true" : "false");

    mapper.endFrame();
    const bool firePressedAfterEnd = mapper.isActionPressed("Fire");
    std::printf("[Input] after endFrame(): isActionPressed(Fire) = %s\n",
                firePressedAfterEnd ? "true" : "false");

    // Unmap before the adapter goes out of scope -- its registered handler
    // captures `this`, so leaving the signal registered would dangle.
    adapter.unmapSignal("ui.button|fire");
}

// ---------------------------------------------------------------------------
// Stage 4: integrate Content + Save + Input + ViewPush in a 5-frame loop
// ---------------------------------------------------------------------------

static void stageFourIntegratedLoop(ActionRtr&                     actions,
                                    Mapper&                        mapper,
                                    ViewPush&                      view,
                                    SaveSchema<demo::GameProgress>& schema,
                                    demo::GameProgress&            progress,
                                    const demo::RecipeBook&        book)
{
    std::printf("\n=== Stage 4: integrated loop (5 frames) ===\n");

    Adapter adapter(actions, mapper);
    adapter.mapSignalToAction("ui.button|advance", "Advance");

    // Pick the active recipe from the loaded content -- shows that gameplay
    // reads authored content rather than hardcoding numbers in C++.
    const demo::Recipe* active = nullptr;
    for (const auto& r : book.recipes) {
        if (r.name == progress.currentRecipe) { active = &r; break; }
    }
    if (!active && !book.recipes.empty()) { active = &book.recipes.front(); }

    constexpr int kFrames = 5;
    for (int frame = 0; frame < kFrames; ++frame) {
        // Simulate a JS button press every frame.
        actions.dispatch("ui.button|advance", "");

        if (mapper.isActionPressed("Advance") && active) {
            progress.score += active->score;
            std::printf("[Loop] frame=%d advance pressed, score=%d\n",
                        frame, progress.score);

            // Push state update to the view layer (printf stub sink).
            view.set("score", std::to_string(progress.score));

            // Re-serialize the save -- in a real game this would be debounced
            // and forwarded to SaveStore, not done every frame.
            const std::string blob = schema.toJsonString(progress);
            std::printf("[Loop] resaved blob length=%zu\n", blob.size());
        }

        mapper.endFrame();
    }

    adapter.unmapSignal("ui.button|advance");
}

// ---------------------------------------------------------------------------
// Stage 5: SchemaValidator -- ContentLoader<T>::loadStringValidated opt-in
// ---------------------------------------------------------------------------

static void stageFiveSchemaValidation()
{
    std::printf("\n=== Stage 5: SchemaValidator (ContentLoader opt-in) ===\n");

    // Build a runtime Schema for demo::Recipe in-code. FieldType uses the
    // engine's actual enum names: String / Float / Int (NOT Number/Integer).
    mitiru::data::Schema schema;
    schema.name    = "recipe";
    schema.version = "1.0";
    schema.fields  = {
        { "name",        mitiru::data::FieldType::String, /*required*/ true },
        { "bakeSeconds", mitiru::data::FieldType::Float,  /*required*/ true },
        { "score",       mitiru::data::FieldType::Int,    /*required*/ true },
    };

    // Positive case: all required fields present and well-typed.
    auto good = ContentLoader<demo::Recipe>::loadStringValidated(
        R"({"name":"Cookie","bakeSeconds":2.5,"score":10})", schema);
    std::printf("[Schema] good case ok=%d", good.ok() ? 1 : 0);
    if (good.ok()) {
        std::printf(" -> %s bake=%.1fs score=%d\n",
            good.value->name.c_str(), good.value->bakeSeconds,
            good.value->score);
    } else {
        std::printf(" error=%s\n", good.error.c_str());
    }

    // Negative case: missing the required `score` field -- validator should
    // reject before from_json runs, and `error` should begin with
    // "schema validation failed:".
    auto bad = ContentLoader<demo::Recipe>::loadStringValidated(
        R"({"name":"NoScore","bakeSeconds":2.5})", schema);
    std::printf("[Schema] bad case ok=%d", bad.ok() ? 1 : 0);
    if (!bad.ok()) {
        std::printf(" error=%s\n", bad.error.c_str());
    } else {
        std::printf(" UNEXPECTED PASS\n");
    }
}

// ---------------------------------------------------------------------------
// Stage 6: SchemaImporter -- draft-07 JSON Schema -> Schema, then validate
// ---------------------------------------------------------------------------

// Inline draft-07 schema for demo::Recipe. In a real game this would live in
// assets/content/recipe.schema.json and be loaded via fromJsonSchemaFile.
static constexpr const char* kRecipeSchemaJson = R"({
    "type": "object",
    "required": ["name", "bakeSeconds", "score"],
    "properties": {
        "name":        { "type": "string"  },
        "bakeSeconds": { "type": "number"  },
        "score":       { "type": "integer" }
    }
})";

static void stageSixReportValidation(const char*  label,
                                     const char*  json,
                                     const mitiru::data::Schema& schema)
{
    auto result = ContentLoader<demo::Recipe>::loadStringValidated(json, schema);
    std::printf("[Schema6] %s case ok=%d", label, result.ok() ? 1 : 0);
    if (result.ok()) {
        std::printf(" -> %s bake=%.1fs score=%d\n",
            result.value->name.c_str(), result.value->bakeSeconds,
            result.value->score);
    } else {
        std::printf(" error=%s\n", result.error.c_str());
    }
}

static void stageSixImportedSchema()
{
    std::printf("\n=== Stage 6: SchemaImporter (draft-07 file-style) ===\n");

    // Import a draft-07 JSON Schema blob into a runtime mitiru::data::Schema.
    // Note the input strings use draft-07 spelling ("integer", "number"), but
    // SchemaImporter maps them onto the engine's FieldType::Int / Float enum.
    auto imported = mitiru::data::SchemaImporter::fromJsonSchemaString(
        kRecipeSchemaJson, "recipe");
    if (!imported.ok()) {
        std::printf("[Schema6] import FAILED: %s\n", imported.error.c_str());
        return;
    }
    const mitiru::data::Schema& schema = *imported.schema;
    std::printf("[Schema6] imported %zu fields\n", schema.fields.size());

    // Positive case: all required fields present and well-typed.
    stageSixReportValidation("good",
        R"({"name":"Tart","bakeSeconds":4.0,"score":15})", schema);

    // Negative case: missing the required `score` field -- validator should
    // reject before from_json runs.
    stageSixReportValidation("bad",
        R"({"name":"NoScore","bakeSeconds":4.0})", schema);
}

// ---------------------------------------------------------------------------
// main -- six stages, each one prints a labelled trace.
// ---------------------------------------------------------------------------

int main()
{
    // BridgeViewPush: stub sinks that printf instead of touching StateStore.
    ViewPush view(
        "save",
        [](std::string_view key, std::string_view val) {
            std::printf("[Bridge.view] %.*s = %.*s\n",
                static_cast<int>(key.size()), key.data(),
                static_cast<int>(val.size()), val.data());
        },
        [](std::string_view key, std::string_view payload) {
            std::printf("[Bridge.emit] %.*s  payload=%.*s\n",
                static_cast<int>(key.size()), key.data(),
                static_cast<int>(payload.size()), payload.data());
        }
    );

    ActionRtr actions;
    Mapper    mapper;

    const demo::RecipeBook            book     = stageOneLoadContent();
    demo::GameProgress                progress = stageTwoSaveRoundTrip();
    SaveSchema<demo::GameProgress>    schema   = buildSaveSchema();

    stageThreeBridgeInput(actions, mapper);
    stageFourIntegratedLoop(actions, mapper, view, schema, progress, book);
    stageFiveSchemaValidation();
    stageSixImportedSchema();

    std::printf("\n[main] data-driven minimal example finished\n");
    return 0;
}
