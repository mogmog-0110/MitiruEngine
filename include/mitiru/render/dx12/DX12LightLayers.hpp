// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12ClusteredLights.hpp の後に include)。光のレイヤー (ADR 0122)
//
// 物のレイヤーは setObjectLightLayers で宣言し、以後の描画の CbDrawEx と影の caster に載る。光のレイヤーは LocalLight::layers で、
// 見える光ごとに CbCluster へ並べる。PS は 2 つが重なる光だけを当て (DX12LitShaders.hpp の lightReaches)、スポットと点光源の影は
// 同じ規則で caster を絞る。0 は「全部」で、宣言しないゲームの絵と費用は変わらない。
//
// 描画は draw run (DX12DrawRun.hpp) に溜まるので、レイヤーを替える前に溜めを描き終える。ひと続きの中では物のレイヤーが変わらず、
// run のキーに入れなくても run の中の描画は同じレイヤーを持つ。

public:
/// @brief 以後に描く物の光のレイヤー (IRenderer3D)。0 = 全部の光に当たる。beginFrame で 0 に戻る
void setObjectLightLayers(std::uint32_t layers) override
{
	flushDrawRun();
	m_drawLightLayers = layers;
}

private:
std::uint32_t m_drawLightLayers = 0;   ///< 以後の描画が属する光のレイヤー。半透明の描画は積んだ時の値を持つ (TransparentDraw::layers)

void beginFrameLightLayers() noexcept { m_drawLightLayers = 0; }
