// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12PointShadows.hpp の後に include)。自作の材質 (ADR 0100)
// 登録時に、陰影ごと (トゥーン、Phong、PBR、照明なし) の PS を FXC で作り、背面を除く PSO と両面の PSO を作る。
// 描画では selectMainPSO が材質の PSO を返し、bindForwardDraw が CbDrawEx の末尾へ値を書く。
// 半透明の描画 (OIT) には材質が効かない。値と秒は setUserMaterial から次の setUserMaterial または beginFrame まで保つ。

public:

/// @brief 自作材質を登録する。同じ名前なら同じ番号を返す。作れなければ -1 を返し、理由は 1 度だけ出す
int registerMaterial3D(const char* name) override
{
	flushDrawRun();
	features::use(features::Feature::UserMaterial);
	if (name == nullptr || name[0] == '\0') { return -1; }
	if (m_needs.declaredMode && !m_needs.wants(features::Feature::UserMaterial))
	{
		features::note(features::Feature::UserMaterial);
		return -1;
	}
	for (std::size_t i = 0; i < m_userMaterials.size(); ++i)
	{
		if (m_userMaterials[i].src.desc.name == name) { return static_cast<int>(i) + 1; }
	}
	if (m_userMaterialFailed.count(name) != 0) { return -1; }
	if (static_cast<int>(m_userMaterials.size()) >= kMaxUserMaterials)
	{
		debug::warnOnce("dx12.usermat.limit", "自作の材質が上限を超えたので、登録する材質を減らしてください");
		return -1;
	}
	std::string error;
	auto src = loadUserMaterialSource(name, error);
	UserMaterialGpu gpu;
	if (!src || !buildUserMaterial(std::move(*src), gpu, error))
	{
		m_userMaterialFailed.insert(name);
		debug::warnOnce(std::string("dx12.usermat.fail.") + name, std::string("自作の材質 ") + name + " を作れないので、-v で出る理由を確かめてください");
		debug::verboseOnce(std::string("dx12.usermat.why.") + name, std::string("自作の材質 ") + name + " の作成に失敗: " + error);
		return -1;
	}
	m_userMaterials.push_back(std::move(gpu));
	return static_cast<int>(m_userMaterials.size());
}

/// @brief 以後の描画に使う材質を設定する。id が 0 なら場面の陰影へ戻す。params は材質の範囲に収めて写す
void setUserMaterial(int id, const float* params, int count, float timeSec) override
{
	flushDrawRun();
	if (id <= 0) { m_activeUserMaterial = 0; return; }
	if (id > static_cast<int>(m_userMaterials.size()))
	{
		debug::verboseOnce("dx12.usermat.unknown_id", "登録していない番号の材質が頼まれたので、場面の陰影で描きます。");
		m_activeUserMaterial = 0;
		return;
	}
	m_activeUserMaterial = id;
	packUserMaterialParams(m_userMaterials[static_cast<std::size_t>(id) - 1].src.desc, params, count, m_userParams);
	m_userTime = std::isfinite(timeSec) ? timeSec : 0.0f;
}

/// @brief 拡張の呼び出し口。kExtReloadUserMaterial は変更されたファイルの材質を作り直す
std::int32_t invokeExtension(std::uint32_t id, void* data, std::uint32_t size) override
{
	flushDrawRun();
	if (id != kExtReloadUserMaterial || data == nullptr || size == 0) { return -1; }
	const char* chars = static_cast<const char*>(data);
	return reloadUserMaterialsFor(std::string(chars, ::strnlen(chars, size)));
}

private:

static constexpr int kUserShadeCount = 4;   ///< トゥーン、Phong、PBR、照明なし
static constexpr int kUserShadeUnlit = 3;

struct UserMaterialGpu
{
	UserMaterialSource src;
	std::size_t sourceHash = 0;
	std::optional<gfx::Dx12Shader> ps[kUserShadeCount];
	ComPtr<ID3D12PipelineState> pso[kUserShadeCount][2];   ///< [陰影][0=背面を除く 1=両面]
};

std::vector<UserMaterialGpu> m_userMaterials;
std::unordered_set<std::string> m_userMaterialFailed;   ///< 作れなかった名前。毎フレームの登録でコンパイルを繰り返さない
int m_activeUserMaterial = 0;                          ///< 以後の描画の材質 (0 = 場面の陰影)
float m_userParams[kMaxUserMaterialParams] = {};
float m_userTime = 0.0f;
int m_userMaterialReloadFailures = 0;

[[nodiscard]] static std::size_t userMaterialHash(const UserMaterialSource& s)
{
	std::string text = s.slang;
	text += s.desc.unlit ? "\nunlit" : "\nscene";
	for (const auto& p : s.desc.params)
	{
		text += "\n" + p.name + ":" + std::to_string(p.defaultValue) + ":" + std::to_string(p.minValue) + ":" + std::to_string(p.maxValue);
	}
	return std::hash<std::string>{}(text);
}

/// 陰影ごとの PS と PSO を作る。失敗時は error に理由を入れて false を返し、out は使えない
[[nodiscard]] bool buildUserMaterial(UserMaterialSource src, UserMaterialGpu& out, std::string& error)
{
	try
	{
		out.src = std::move(src);
		out.sourceHash = userMaterialHash(out.src);
		const bool unlit = out.src.desc.unlit;
		for (int i = 0; i < kUserShadeCount; ++i)
		{
			if (unlit && i != kUserShadeUnlit) { continue; }
			UserMaterialSource variant = out.src;
			variant.desc.unlit = (i == kUserShadeUnlit);
			const LitShade shade = (i == 1) ? LitShade::Phong : (i == 2) ? LitShade::Pbr : LitShade::Toon;
			out.ps[i] = gfx::Dx12Shader::createPixelShader(dx12UserMaterialPixelShader(shade, variant), "PSMain");
		}
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		fillForwardPsoState(psoDesc);
		psoDesc.VS = m_toonVS->shaderBytecode();
		D3D12_INPUT_ELEMENT_DESC inputLayout[4] = {};
		UINT inputCount = 0;
		getInputLayout(inputLayout, inputCount);
		psoDesc.InputLayout.pInputElementDescs = inputLayout;
		psoDesc.InputLayout.NumElements = inputCount;
		for (int i = 0; i < kUserShadeCount; ++i)
		{
			if (!out.ps[i]) { continue; }
			for (int side = 0; side < 2; ++side)
			{
				psoDesc.RasterizerState.CullMode = side == 0 ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
				psoDesc.PS = out.ps[i]->shaderBytecode();
				const HRESULT hr = m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(out.pso[i][side].GetAddressOf()));
				if (FAILED(hr)) { throw std::runtime_error("CreateGraphicsPipelineState に失敗しました"); }
			}
		}
		if (unlit)
		{
			for (int i = 0; i < kUserShadeUnlit; ++i)
			{
				for (int side = 0; side < 2; ++side) { out.pso[i][side] = out.pso[kUserShadeUnlit][side]; }
			}
		}
		return true;
	}
	catch (const std::exception& e)
	{
		error = e.what();
		return false;
	}
}

/// @brief 現在の材質と陰影、両面かどうかに合う PSO。材質がない、または作れていなければ nullptr
[[nodiscard]] ID3D12PipelineState* userMaterialPso(bool doubleSided) const noexcept
{
	if (m_activeUserMaterial <= 0 || m_activeUserMaterial > static_cast<int>(m_userMaterials.size())) { return nullptr; }
	int shade = 0;
	switch (m_shaderMode)
	{
	case ShaderMode3D::Phong: shade = 1; break;
	case ShaderMode3D::PBR: shade = 2; break;
	case ShaderMode3D::Unlit:
	case ShaderMode3D::Flat: shade = kUserShadeUnlit; break;
	default: break;
	}
	return m_userMaterials[static_cast<std::size_t>(m_activeUserMaterial) - 1].pso[shade][doubleSided ? 1 : 0].Get();
}

/// @brief 材質があるときだけ呼び、CbDrawEx の末尾へ現在の材質の値を書く
void writeUserMaterialCb(DX12CbDrawEx& cb) const noexcept
{
	for (int i = 0; i < kMaxUserMaterialParams; ++i) { cb.userParams[i / 4][i % 4] = m_userParams[i]; }
	cb.userInfo[0] = m_userTime;
}

void beginFrameUserMaterial() noexcept { m_activeUserMaterial = 0; }

/// @brief changedPath を元にした材質を作り直す。成功時だけ入れ替え、失敗時は前のままにする。作り直した数を返す
int reloadUserMaterialsFor(const std::string& changedPath)
{
	const auto norm = [](std::string s) {
		for (char& c : s) { c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
		return s;
	};
	const auto endsWith = [](const std::string& s, const std::string& tail) {
		return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
	};
	const std::string changed = norm(changedPath);
	int reloaded = 0;
	std::vector<std::string> names;
	for (const auto& m : m_userMaterials) { names.push_back(m.src.desc.name); }
	for (const auto& failed : m_userMaterialFailed) { names.push_back(failed); }
	for (const std::string& name : names)
	{
		if (!endsWith(changed, norm(userMaterialPath(name, ".slang"))) && !endsWith(changed, norm(userMaterialPath(name, ".mat.json")))) { continue; }
		const bool registered = std::any_of(m_userMaterials.begin(), m_userMaterials.end(),
		                                    [&name](const UserMaterialGpu& m) { return m.src.desc.name == name; });
		if (!registered && static_cast<int>(m_userMaterials.size()) >= kMaxUserMaterials) { continue; }
		std::string error;
		auto src = loadUserMaterialSource(name, error);
		UserMaterialGpu fresh;
		if (!src || !buildUserMaterial(std::move(*src), fresh, error))
		{
			debug::warnOnce(std::string("dx12.usermat.reload.") + name + std::to_string(++m_userMaterialReloadFailures),
			                std::string("自作の材質 ") + name + " を読み込み直せないので、-v で出る理由を確かめてください");
			console::verbosef("自作の材質 %s の読み込みに失敗: %s", name.c_str(), error.c_str());
			continue;
		}
		if (m_device != nullptr) { m_device->waitForGpu(); }   // 前の PSO を GPU が読み終えてから手放す
		m_userMaterialFailed.erase(name);
		bool replaced = false;
		for (auto& m : m_userMaterials)
		{
			if (m.src.desc.name == name) { m = std::move(fresh); replaced = true; break; }
		}
		if (!replaced) { m_userMaterials.push_back(std::move(fresh)); }
		++reloaded;
		console::verbosef("自作の材質 %s を読み込み直しました。", name.c_str());
	}
	return reloaded;
}
