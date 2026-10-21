#pragma once
// RmlUi の RenderInterface を Dawn (WebGPU) 上で使うための宣言。MITIRU_WITH_DAWN=ON のビルドだけで使う。
// DawnDevice の最終色 (RGBA8) に、そのフレームの UI を重ねる。公開するのは RmlUi の型だけで、Dawn の型は src/dawn/ に閉じる。

#include <memory>
#include <string>

#include <RmlUi/Core/RenderInterface.h>

#include <mitiru/ui_rml/RmlUiHost.hpp>

namespace mitiru::gfx
{
class DawnDevice;
}

namespace mitiru::ui_rml
{

class RmlRenderInterfaceDawn : public Rml::RenderInterface
{
public:
	/// @return 作成できなければ nullptr を返し、error に内容を入れる
	[[nodiscard]] static std::unique_ptr<RmlRenderInterfaceDawn> create(gfx::DawnDevice& device, std::string& error);

	/// @param width,height 描画先 (DawnDevice の最終色) の実画素数
	/// @param logical Rml::Context の大きさ
	virtual bool beginFrame(int width, int height, Rml::Vector2i logical) = 0;
	/// そのフレームの描画を GPU へ出す
	virtual void endFrame() = 0;
	/// "view3d:N" の画像の出どころ。描くたびに fn(ctx, N) で引き直す
	virtual void setExternalImageSource(UiExternalImageDawnFn fn, void* ctx) = 0;
};

} // namespace mitiru::ui_rml
