#pragma once
// --capture-input の台本。撮影の途中で、本物の窓と同じ口 (ToolUiHost::processInput) へマウスとキーを渡す。
// 書式は "フレーム:click:X,Y" / "フレーム:type:文字" / "フレーム:key:仮想キーコード" を ; で並べたもの。
// 座標は描画先の画素。click はそのフレームで押し、次のフレームで離す。

#include "ui/ToolUiHost.hpp"

#include <mitiru/platform/win32/Win32KeyMessage.hpp>

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace mitiru::tool
{

class CaptureInput
{
public:
	explicit CaptureInput(std::string_view script)
	{
		std::size_t start = 0;
		while (start < script.size())
		{
			const std::size_t end = std::min(script.find(';', start), script.size());
			parseStep(script.substr(start, end - start));
			start = end + 1;
		}
	}

	/// そのフレームに渡すマウスとキー。keys はこのオブジェクトが持つので次の呼び出しまで有効。
	ToolPointer pointerAt(int frame, std::vector<platform::Win32KeyMessage>& keys)
	{
		keys.clear();
		m_pointer.buttons[0] = false;
		for (const Step& s : m_steps)
		{
			if (s.frame != frame) { continue; }
			if (s.kind == Kind::Click)
			{
				m_pointer.x = s.x;
				m_pointer.y = s.y;
				m_pointer.buttons[0] = true;
			}
			else if (s.kind == Kind::Type) { appendText(keys, s.text); }
			else
			{
				keys.push_back({ platform::kWmKeyDown, static_cast<std::uint32_t>(s.vk), 1, 0 });
				keys.push_back({ platform::kWmKeyUp, static_cast<std::uint32_t>(s.vk), 1, 0 });
			}
		}
		return m_pointer;
	}

private:
	enum class Kind { Click, Type, Key };
	struct Step
	{
		int frame = 0;
		Kind kind = Kind::Click;
		float x = 0.0f;
		float y = 0.0f;
		std::string text;
		int vk = 0;
	};

	void parseStep(std::string_view step)
	{
		const auto c1 = step.find(':');
		const auto c2 = c1 == std::string_view::npos ? c1 : step.find(':', c1 + 1);
		if (c2 == std::string_view::npos) { return; }
		Step s;
		s.frame = std::atoi(std::string(step.substr(0, c1)).c_str());
		const std::string_view verb = step.substr(c1 + 1, c2 - c1 - 1);
		const std::string arg(step.substr(c2 + 1));
		if (verb == "click")
		{
			s.kind = Kind::Click;
			s.x = static_cast<float>(std::atof(arg.c_str()));
			const auto comma = arg.find(',');
			s.y = comma == std::string::npos ? 0.0f : static_cast<float>(std::atof(arg.c_str() + comma + 1));
		}
		else if (verb == "type") { s.kind = Kind::Type; s.text = arg; }
		else if (verb == "key")  { s.kind = Kind::Key; s.vk = std::atoi(arg.c_str()); }
		else { return; }
		m_steps.push_back(std::move(s));
	}

	static void appendText(std::vector<platform::Win32KeyMessage>& keys, const std::string& utf8)
	{
		const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
		std::wstring wide(static_cast<std::size_t>(n), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), n);
		for (const wchar_t ch : wide) { keys.push_back({ platform::kWmChar, static_cast<std::uint32_t>(ch), 1, 0 }); }
	}

	std::vector<Step> m_steps;
	ToolPointer m_pointer;
};

} // namespace mitiru::tool
