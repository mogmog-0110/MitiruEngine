#include <catch2/catch_test_macros.hpp>
#include <sgc/scripting/ScriptBindings.hpp>

#include <string>
#include <vector>

using namespace sgc::scripting;

// テスト用スクリプトエンジン実装
class MockScriptEngine : public IScriptEngine
{
public:
	ScriptValue eval(const std::string& code) override
	{
		m_lastEvalCode = code;
		return m_evalResult;
	}

	bool registerFunction(const std::string& name, ScriptFunction fn) override
	{
		m_functions[name] = fn;
		return true;
	}

	bool registerVariable(const std::string& name, ScriptValue val) override
	{
		m_variables[name] = val;
		return true;
	}

	// テスト用アクセサ
	void setEvalResult(ScriptValue val) { m_evalResult = val; }
	const std::string& lastEvalCode() const { return m_lastEvalCode; }

	bool hasFunction(const std::string& name) const
	{
		return m_functions.find(name) != m_functions.end();
	}

	bool hasVariable(const std::string& name) const
	{
		return m_variables.find(name) != m_variables.end();
	}

	ScriptValue callFunction(const std::string& name, const std::vector<ScriptValue>& args)
	{
		return m_functions.at(name)(args);
	}

	ScriptValue getVariable(const std::string& name) const
	{
		return m_variables.at(name);
	}

private:
	ScriptValue m_evalResult = 0;
	std::string m_lastEvalCode;
	std::unordered_map<std::string, ScriptFunction> m_functions;
	std::unordered_map<std::string, ScriptValue> m_variables;
};

TEST_CASE("ScriptValue holds int", "[scripting]")
{
	ScriptValue val = 42;
	REQUIRE(std::holds_alternative<int>(val));
	REQUIRE(getInt(val) == 42);
	REQUIRE(getTypeName(val) == "int");
}

TEST_CASE("ScriptValue holds float", "[scripting]")
{
	ScriptValue val = 3.14f;
	REQUIRE(std::holds_alternative<float>(val));
	REQUIRE(getFloat(val) == 3.14f);
	REQUIRE(getTypeName(val) == "float");
}

TEST_CASE("ScriptValue holds bool", "[scripting]")
{
	ScriptValue val = true;
	REQUIRE(std::holds_alternative<bool>(val));
	REQUIRE(getBool(val) == true);
	REQUIRE(getTypeName(val) == "bool");
}

TEST_CASE("ScriptValue holds string", "[scripting]")
{
	ScriptValue val = std::string("hello");
	REQUIRE(std::holds_alternative<std::string>(val));
	REQUIRE(getString(val) == "hello");
	REQUIRE(getTypeName(val) == "string");
}

TEST_CASE("ScriptValue getters return default for wrong type", "[scripting]")
{
	ScriptValue intVal = 42;
	REQUIRE(getFloat(intVal) == 0.0f);
	REQUIRE(getBool(intVal) == false);
	REQUIRE(getString(intVal) == "");

	ScriptValue strVal = std::string("test");
	REQUIRE(getInt(strVal) == 0);
}

TEST_CASE("IScriptEngine eval and register", "[scripting]")
{
	MockScriptEngine engine;
	engine.setEvalResult(ScriptValue{100});

	const auto result = engine.eval("1 + 2");
	REQUIRE(engine.lastEvalCode() == "1 + 2");
	REQUIRE(getInt(result) == 100);

	// 関数登録
	engine.registerFunction("add", [](const std::vector<ScriptValue>& args) -> ScriptValue
	{
		return getInt(args[0]) + getInt(args[1]);
	});
	REQUIRE(engine.hasFunction("add"));

	const auto addResult = engine.callFunction("add", {ScriptValue{3}, ScriptValue{4}});
	REQUIRE(getInt(addResult) == 7);

	// 変数登録
	engine.registerVariable("pi", ScriptValue{3.14f});
	REQUIRE(engine.hasVariable("pi"));
	REQUIRE(getFloat(engine.getVariable("pi")) == 3.14f);
}

TEST_CASE("ScriptFunction with no args returns value", "[scripting]")
{
	ScriptFunction fn = [](const std::vector<ScriptValue>&) -> ScriptValue
	{
		return std::string("constant");
	};

	const auto result = fn({});
	REQUIRE(getString(result) == "constant");
}
