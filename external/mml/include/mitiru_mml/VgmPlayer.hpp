#pragma once
/// @file VgmPlayer.hpp
/// @brief VGMファイルプレイヤー（YM2608対応）
/// @details VGMファイルを読み込み、ymfmでYM2608をエミュレートしてPCMを生成する。
///          vgmrender.cppのアーキテクチャを忠実に再現し、固定小数点タイミングと
///          正確なFM+SSGミキシングを行う。
///
/// @code
/// mitiru_mml::VgmPlayer vgm;
/// if (vgm.loadFile("assets/vgm/01_Prologue.vgm"))
/// {
///     auto pcm = vgm.render();
///     mitiru_mml::AudioOutput::play(pcm, vgm.sampleRate());
/// }
/// @endcode

#include <mitiru_mml/MmlTypes.hpp>

#include <ymfm_opn.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
// compressapi.hのCOMPRESS_ALGORITHM_*定数が見えない環境向けのフォールバック
// （_WIN32_WINNTが0x0602未満で定義されている場合に発生する）
#ifndef COMPRESS_ALGORITHM_DEFLATE
#define COMPRESS_ALGORITHM_DEFLATE 2
#define COMPRESS_RAW 0x20000000
// compressapi.hの関数を手動宣言する
extern "C"
{
	typedef PVOID COMPRESSOR_HANDLE;
	typedef PVOID DECOMPRESSOR_HANDLE;

	__declspec(dllimport) BOOL WINAPI CreateDecompressor(
		DWORD Algorithm,
		PVOID AllocationRoutines,
		DECOMPRESSOR_HANDLE* DecompressorHandle);

	__declspec(dllimport) BOOL WINAPI Decompress(
		DECOMPRESSOR_HANDLE DecompressorHandle,
		LPCVOID CompressedData,
		SIZE_T CompressedDataSize,
		PVOID UncompressedBuffer,
		SIZE_T UncompressedBufferSize,
		SIZE_T* UncompressedDataSize);

	__declspec(dllimport) BOOL WINAPI CloseDecompressor(
		DECOMPRESSOR_HANDLE DecompressorHandle);
}
#else
#include <compressapi.h>
#endif
#pragma comment(lib, "cabinet.lib")
#endif

namespace mitiru_mml
{

/// @brief gzip圧縮データを展開する（VGZファイル用）
/// @details gzipヘッダーを解析し、Windows Compression APIでdeflateデータを展開する。
///          Windows 8以降で利用可能。
/// @param gzData gzip圧縮データ
/// @param outData 展開先バッファ（成功時にリサイズされる）
/// @return 展開成功ならtrue
[[nodiscard]] inline bool decompressGzip(
	const std::vector<uint8_t>& gzData,
	std::vector<uint8_t>& outData)
{
#ifdef _WIN32
	// gzipマジックバイトを確認する（0x1F 0x8B）
	// 最小サイズ: 10バイトヘッダー + 8バイトトレーラー = 18バイト
	if (gzData.size() < 18 || gzData[0] != 0x1F || gzData[1] != 0x8B)
	{
		return false;
	}

	// gzipヘッダーを解析してdeflateデータの開始位置を特定する
	std::size_t pos = 10;
	const uint8_t flags = gzData[3];

	if (flags & 0x04) // FEXTRA
	{
		if (pos + 2 > gzData.size()) return false;
		const uint16_t xlen = static_cast<uint16_t>(
			gzData[pos] | (gzData[pos + 1] << 8));
		pos += 2 + xlen;
	}
	if (flags & 0x08) // FNAME
	{
		while (pos < gzData.size() && gzData[pos] != 0) ++pos;
		++pos;
	}
	if (flags & 0x10) // FCOMMENT
	{
		while (pos < gzData.size() && gzData[pos] != 0) ++pos;
		++pos;
	}
	if (flags & 0x02) // FHCRC
	{
		pos += 2;
	}

	if (pos >= gzData.size() - 8)
	{
		return false;
	}

	// gzip末尾4バイトから元データサイズを取得する（mod 2^32）
	const uint32_t origSize =
		static_cast<uint32_t>(gzData[gzData.size() - 4])
		| (static_cast<uint32_t>(gzData[gzData.size() - 3]) << 8)
		| (static_cast<uint32_t>(gzData[gzData.size() - 2]) << 16)
		| (static_cast<uint32_t>(gzData[gzData.size() - 1]) << 24);

	// Windows Compression APIでdeflateデータを展開する
	DECOMPRESSOR_HANDLE decompressor = NULL;
	if (!CreateDecompressor(COMPRESS_ALGORITHM_DEFLATE, NULL, &decompressor))
	{
		return false;
	}

	outData.resize(origSize);
	SIZE_T decompressedSize = 0;
	const BOOL success = Decompress(
		decompressor,
		gzData.data() + pos,
		gzData.size() - pos - 8, // ヘッダーとトレーラーをスキップする
		outData.data(),
		origSize,
		&decompressedSize);

	CloseDecompressor(decompressor);

	if (!success)
	{
		outData.clear();
		return false;
	}

	outData.resize(decompressedSize);
	return true;
#else
	// Windows以外ではVGZ非対応
	(void)gzData;
	(void)outData;
	return false;
#endif
}

/// @brief VGM用ymfmインターフェース（ADPCMデータブロック対応）
/// @details VGMデータブロック（0x67）で送られるADPCM-A/B ROMデータを保持し、
///          ymfmからの外部メモリ読み出し・書き込み要求に応答する。
class VgmOpnaInterface : public ymfm::ymfm_interface
{
public:
	/// @brief ADPCM-A（リズム音源）ROMデータ
	std::vector<uint8_t> m_adpcmA;

	/// @brief ADPCM-B（DELTA-T）ROMデータ
	std::vector<uint8_t> m_adpcmB;

	/// @brief 外部メモリ読み出しコールバック
	/// @param type アクセス種別
	/// @param address 読み出しアドレス
	/// @return 読み出しデータ
	uint8_t ymfm_external_read(ymfm::access_class type, uint32_t address) override
	{
		if (type == ymfm::ACCESS_ADPCM_A)
		{
			return (address < m_adpcmA.size()) ? m_adpcmA[address] : 0;
		}
		if (type == ymfm::ACCESS_ADPCM_B)
		{
			return (address < m_adpcmB.size()) ? m_adpcmB[address] : 0;
		}
		return 0;
	}

	/// @brief 外部メモリ書き込みコールバック（ADPCM-B書き込み対応）
	/// @param type アクセス種別
	/// @param address 書き込みアドレス
	/// @param data 書き込みデータ
	void ymfm_external_write(ymfm::access_class type, uint32_t address, uint8_t data) override
	{
		if (type == ymfm::ACCESS_ADPCM_B)
		{
			if (address >= m_adpcmB.size())
			{
				m_adpcmB.resize(address + 1, 0);
			}
			m_adpcmB[address] = data;
		}
	}
};

/// @brief レジスタ書き込みキューの要素
/// @details ポート0はreg=0x00〜0xFF、ポート1はreg=0x100〜0x1FF
struct VgmRegWrite
{
	uint32_t reg;  ///< レジスタ番号（0x100以上はポート1）
	uint8_t val;   ///< 書き込み値
};

/// @brief VGMファイルプレイヤー
/// @details VGMファイルを読み込み、YM2608レジスタ書き込みコマンドを解析して
///          ymfmでPCMを生成する。vgmrender.cppと同等の固定小数点タイミング、
///          レジスタ書き込みキュー、FM+SSGミキシングを実装する。
class VgmPlayer
{
public:
	/// @brief PC-98標準OPNAクロック（7.9872MHz）
	static constexpr uint32_t OPNA_CLOCK = 7987200;

	/// @brief 出力サンプルレート（VGM標準）
	static constexpr uint32_t OUTPUT_RATE = 44100;

	/// @brief 最大レンダリング時間（秒）— 無限ループ防止
	static constexpr float MAX_RENDER_SECONDS = 300.0f;

	/// @brief VGMファイルを読み込む（非圧縮VGMのみ）
	/// @param path ファイルパス
	/// @return 読み込み成功ならtrue
	bool loadFile(const std::string& path)
	{
		std::ifstream ifs(path, std::ios::binary | std::ios::ate);
		if (!ifs) return false;

		const auto size = ifs.tellg();
		if (size < 0x40) return false;

		ifs.seekg(0);
		m_data.resize(static_cast<std::size_t>(size));
		ifs.read(reinterpret_cast<char*>(m_data.data()), size);

		return parseHeader();
	}

	/// @brief VGM/VGZファイルを自動判別して読み込む
	/// @details gzipマジックバイト（0x1F 0x8B）を検出した場合はVGZとして
	///          自動展開する。それ以外は通常のVGMとして読み込む。
	/// @param path ファイルパス（.vgm または .vgz）
	/// @return 読み込み成功ならtrue
	bool loadFileAuto(const std::string& path)
	{
		std::ifstream ifs(path, std::ios::binary | std::ios::ate);
		if (!ifs) return false;

		const auto size = ifs.tellg();
		if (size < 2) return false;

		ifs.seekg(0);
		std::vector<uint8_t> fileData(static_cast<std::size_t>(size));
		ifs.read(reinterpret_cast<char*>(fileData.data()), size);

		// gzipマジックバイトを検出する
		if (fileData.size() >= 18
			&& fileData[0] == 0x1F && fileData[1] == 0x8B)
		{
			std::vector<uint8_t> decompressed;
			if (decompressGzip(fileData, decompressed))
			{
				m_data = std::move(decompressed);
				return parseHeader();
			}
			return false;
		}

		// 通常のVGMとして読み込む
		m_data = std::move(fileData);
		return parseHeader();
	}

	/// @brief メモリからVGMデータを読み込む
	/// @param data VGMデータの先頭ポインタ
	/// @param size データサイズ（バイト）
	/// @return 読み込み成功ならtrue
	bool loadMemory(const uint8_t* data, std::size_t size)
	{
		if (size < 0x40) return false;
		m_data.assign(data, data + size);
		return parseHeader();
	}

	/// @brief VGM全体をレンダリングしてPCMバッファを返す
	/// @details vgmrender.cppのアーキテクチャに従い、以下の手順で生成する:
	///          1. VGMコマンドを順次解析する
	///          2. レジスタ書き込みはキューに積む
	///          3. Wait命令で指定された44100Hzサンプル数だけ出力を生成する
	///          4. 各出力サンプル生成時にキューから1つずつレジスタ書き込みを処理する
	///          5. 固定小数点アキュムレータでチップネイティブレート→44100Hz変換する
	///          6. FM_L + SSG / FM_R + SSG でミキシング（SSGはスケーリングしない）
	/// @param maxSeconds 最大レンダリング時間（秒）。0で既定値使用
	/// @return 16bitモノラルPCMバッファ（44100Hz）
	[[nodiscard]] PcmBuffer render(float maxSeconds = 0.0f) const
	{
		if (m_data.empty() || m_dataOffset == 0) return {};
		if (maxSeconds <= 0.0f) maxSeconds = MAX_RENDER_SECONDS;

		const uint32_t maxOutputSamples =
			static_cast<uint32_t>(maxSeconds * static_cast<float>(OUTPUT_RATE));

		// ymfmインターフェースとチップを構築する
		VgmOpnaInterface iface;

		// ADPCMデータブロックを事前にロードする
		loadDataBlocks(iface);

		ymfm::ym2608 chip(iface);
		chip.reset();

		// 6チャンネルモードを有効にする
		chip.write_address(0x29);
		chip.write_data(0x9F);

		// チップのネイティブサンプルレートを取得する
		const uint32_t nativeRate = chip.sample_rate(m_ym2608Clock);

		// vgmrender方式: 32.32固定小数点タイミング
		// m_step = 0x100000000 / nativeRate
		// 各出力サンプル(44100Hz)生成時にアキュムレータを進め、
		// チップのネイティブサンプルを必要数だけ生成する
		const uint64_t step = 0x100000000ull / static_cast<uint64_t>(nativeRate);
		uint64_t chipPos = 0;

		ymfm::ym2608::output_data output;
		std::memset(&output, 0, sizeof(output));

		PcmBuffer result;
		result.reserve(std::min(maxOutputSamples, OUTPUT_RATE * 60u));

		std::deque<VgmRegWrite> writeQueue;
		int loopsRemaining = 1;

		std::size_t pos = m_dataOffset;

		while (pos < m_data.size() && result.size() < maxOutputSamples)
		{
			// Wait以外のコマンドを処理してキューに積む
			uint32_t samplesRemaining = 0;

			while (pos < m_data.size() && samplesRemaining == 0)
			{
				const uint8_t cmd = m_data[pos];

				if (cmd == 0x56 || cmd == 0xA6) // YM2608 ポート0
				{
					if (pos + 2 >= m_data.size()) goto done;
					writeQueue.push_back({m_data[pos + 1], m_data[pos + 2]});
					pos += 3;
				}
				else if (cmd == 0x57 || cmd == 0xA7) // YM2608 ポート1
				{
					if (pos + 2 >= m_data.size()) goto done;
					writeQueue.push_back({0x100u | m_data[pos + 1], m_data[pos + 2]});
					pos += 3;
				}
				else if (cmd == 0x61) // Wait N samples
				{
					if (pos + 2 >= m_data.size()) goto done;
					samplesRemaining = static_cast<uint16_t>(
						m_data[pos + 1] | (m_data[pos + 2] << 8));
					pos += 3;
				}
				else if (cmd == 0x62) // Wait 735 samples (NTSC 1/60s)
				{
					samplesRemaining = 735;
					pos += 1;
				}
				else if (cmd == 0x63) // Wait 882 samples (PAL 1/50s)
				{
					samplesRemaining = 882;
					pos += 1;
				}
				else if (cmd >= 0x70 && cmd <= 0x7F) // Wait 1〜16 samples
				{
					samplesRemaining = (cmd & 0x0F) + 1;
					pos += 1;
				}
				else if (cmd == 0x66) // End of data
				{
					if (m_loopOffset > 0 && loopsRemaining > 0)
					{
						pos = m_loopOffset;
						--loopsRemaining;
					}
					else
					{
						goto done;
					}
				}
				else if (cmd == 0x67) // Data block（既にloadDataBlocksで処理済み、スキップ）
				{
					if (pos + 6 >= m_data.size()) goto done;
					const uint32_t blockSize = readUint32(pos + 3);
					pos = pos + 7 + blockSize;
				}
				else if (cmd >= 0x30 && cmd <= 0x3F) // 1バイト引数コマンド（非対応チップ）
				{
					pos += 2;
				}
				else if (cmd == 0x4F || cmd == 0x50) // PSG系（1バイト引数）
				{
					pos += 2;
				}
				else if (cmd >= 0x40 && cmd <= 0x4E) // 2バイト引数コマンド（非対応チップ）
				{
					pos += 3;
				}
				else if ((cmd >= 0x51 && cmd <= 0x5F) || (cmd >= 0xA0 && cmd <= 0xBF))
				{
					// 他のチップ書き込みコマンド（2バイト引数）— スキップ
					pos += 3;
				}
				else if (cmd >= 0xC0 && cmd <= 0xDF)
				{
					// 3バイト引数コマンド — スキップ
					pos += 4;
				}
				else if (cmd >= 0xE0 && cmd <= 0xFF)
				{
					// 4バイト引数コマンド — スキップ
					pos += 5;
				}
				else
				{
					// 不明コマンド — 1バイトスキップ
					pos += 1;
				}
			}

			// 44100Hzサンプルを生成する
			// 各出力サンプルにつき:
			//   1. キューからレジスタ書き込みを1つ処理する
			//   2. 固定小数点アキュムレータでチップを進める
			//   3. FM+SSGをミキシングする
			for (uint32_t i = 0; i < samplesRemaining && result.size() < maxOutputSamples; ++i)
			{
				// vgmrender方式: 1出力サンプルにつき1レジスタ書き込み
				if (!writeQueue.empty())
				{
					const auto& w = writeQueue.front();
					if (w.reg < 0x100)
					{
						chip.write_address(static_cast<uint8_t>(w.reg));
						chip.write_data(w.val);
					}
					else
					{
						chip.write_address_hi(static_cast<uint8_t>(w.reg & 0xFF));
						chip.write_data_hi(w.val);
					}
					writeQueue.pop_front();
				}

				// vgmrender方式: 固定小数点タイミングでチップサンプルを生成する
				// output_start = 出力サンプルに対応するチップ時間位置
				// 44100Hz→nativeRate変換
				const uint64_t outputStart =
					static_cast<uint64_t>(result.size() + 1) * 0x100000000ull / OUTPUT_RATE;

				while (chipPos <= outputStart)
				{
					chip.generate(&output);
					chipPos += step;
				}

				// vgmrender.cppと完全一致するミキシング:
				// YM2608: data[0]=FM Left, data[1]=FM Right, data[2]=SSG
				// Left  = FM_L + SSG（SSGはスケーリングしない）
				// Right = FM_R + SSG
				// モノラル = (FM_L + FM_R) / 2 + SSG
				const int32_t fmL = output.data[0];
				const int32_t fmR = output.data[1];
				const int32_t ssg = output.data[2];
				int32_t mono = (fmL + fmR) / 2 + ssg;

				// ハードクランプ（vgmrender同様、ソフトクリッピングなし）
				if (mono > 32767) mono = 32767;
				if (mono < -32768) mono = -32768;

				result.push_back(static_cast<int16_t>(mono));
			}
		}

	done:
		return result;
	}

	/// @brief 出力サンプルレートを返す（44100Hz固定）
	/// @return サンプルレート(Hz)
	[[nodiscard]] uint32_t sampleRate() const noexcept
	{
		return OUTPUT_RATE;
	}

	/// @brief VGMが読み込まれているか
	/// @return 読み込み済みならtrue
	[[nodiscard]] bool loaded() const noexcept
	{
		return !m_data.empty() && m_dataOffset > 0;
	}

	/// @brief VGMのタイトル情報（GD3タグ）
	/// @return タイトル文字列（ASCII部分のみ）
	[[nodiscard]] const std::string& title() const noexcept
	{
		return m_title;
	}

	/// @brief VGMの総サンプル数（ヘッダーから取得）
	/// @return 総サンプル数（44100Hz基準）
	[[nodiscard]] uint32_t totalSamples() const noexcept
	{
		return m_totalSamples;
	}

	/// @brief VGMの推定再生時間（秒）
	/// @return 再生時間（秒）
	[[nodiscard]] float estimatedDuration() const noexcept
	{
		if (m_totalSamples == 0) return 0.0f;
		return static_cast<float>(m_totalSamples) / 44100.0f;
	}

private:
	/// @brief リトルエンディアンの32bit値を読み取る
	/// @param offset データ内のバイトオフセット
	/// @return 32bit値
	[[nodiscard]] uint32_t readUint32(std::size_t offset) const noexcept
	{
		if (offset + 3 >= m_data.size()) return 0;
		return static_cast<uint32_t>(m_data[offset])
			| (static_cast<uint32_t>(m_data[offset + 1]) << 8)
			| (static_cast<uint32_t>(m_data[offset + 2]) << 16)
			| (static_cast<uint32_t>(m_data[offset + 3]) << 24);
	}

	/// @brief VGMヘッダーを解析する
	/// @return 解析成功ならtrue
	bool parseHeader()
	{
		if (m_data.size() < 0x40) return false;

		// マジック "Vgm " を確認する
		if (m_data[0] != 'V' || m_data[1] != 'g'
			|| m_data[2] != 'm' || m_data[3] != ' ')
		{
			return false;
		}

		// +18: 総サンプル数（44100Hz基準）
		m_totalSamples = readUint32(0x18);

		// +1C: ループオフセット（0x1Cからの相対値）
		const uint32_t loopOff = readUint32(0x1C);
		m_loopOffset = (loopOff > 0) ? (0x1C + loopOff) : 0;

		// +34: VGMデータオフセット（0x34からの相対値）
		const uint32_t dataOff = readUint32(0x34);
		const uint32_t version = readUint32(0x08);
		if (version < 0x150 || dataOff == 0)
		{
			m_dataOffset = 0x40;
		}
		else
		{
			m_dataOffset = 0x34 + dataOff;
		}

		// +48: YM2608クロック値を読み取る
		m_ym2608Clock = 0;
		if (m_data.size() >= 0x4C)
		{
			m_ym2608Clock = readUint32(0x48);
		}
		if (m_ym2608Clock == 0)
		{
			m_ym2608Clock = OPNA_CLOCK;
		}

		// GD3タグからタイトルを読み取る
		parseGd3();

		return true;
	}

	/// @brief GD3タグを解析してタイトルを抽出する
	void parseGd3()
	{
		m_title.clear();

		// +14: GD3オフセット（0x14からの相対値）
		const uint32_t gd3Off = readUint32(0x14);
		if (gd3Off == 0) return;

		const std::size_t gd3Pos = 0x14 + gd3Off;
		if (gd3Pos + 12 >= m_data.size()) return;

		// GD3マジック "Gd3 " を確認する
		if (m_data[gd3Pos] != 'G' || m_data[gd3Pos + 1] != 'd'
			|| m_data[gd3Pos + 2] != '3' || m_data[gd3Pos + 3] != ' ')
		{
			return;
		}

		// GD3 version (4bytes) + size (4bytes) の後に文字列群（UTF-16LE）
		const std::size_t strStart = gd3Pos + 12;

		// 最初の文字列がトラック名（英語）
		std::size_t strPos = strStart;
		while (strPos + 1 < m_data.size())
		{
			const uint16_t ch = static_cast<uint16_t>(
				m_data[strPos] | (m_data[strPos + 1] << 8));
			if (ch == 0) break;

			if (ch < 128)
			{
				m_title += static_cast<char>(ch);
			}
			else
			{
				m_title += '?';
			}
			strPos += 2;
		}
	}

	/// @brief VGMデータブロック（0x67）からADPCMデータを事前にロードする
	/// @details VGMストリームを走査し、データブロックコマンドのみを処理する。
	///          type 0x81 = YM2608 ADPCM-B (DELTA-T) ROM
	///          type 0x82 = YM2608 ADPCM-A（リズム音源）ROM
	/// @param iface 書き込み先のymfmインターフェース
	void loadDataBlocks(VgmOpnaInterface& iface) const
	{
		std::size_t pos = m_dataOffset;

		while (pos < m_data.size())
		{
			const uint8_t cmd = m_data[pos];

			if (cmd == 0x67) // Data block
			{
				if (pos + 6 >= m_data.size()) break;

				const uint8_t blockType = m_data[pos + 2];
				const uint32_t blockSize = readUint32(pos + 3);
				const std::size_t dataPos = pos + 7;

				if (dataPos + blockSize > m_data.size()) break;

				if (blockType == 0x81 && blockSize > 8)
				{
					// YM2608 ADPCM-B (DELTA-T) ROM
					// フォーマット: [ROM size 4B][start addr 4B][data...]
					const uint32_t startAddr = readUint32(dataPos + 4);
					const uint32_t payloadSize = blockSize - 8;
					if (dataPos + 8 + payloadSize <= m_data.size())
					{
						const uint32_t endAddr = startAddr + payloadSize;
						if (endAddr > iface.m_adpcmB.size())
						{
							iface.m_adpcmB.resize(endAddr, 0);
						}
						std::memcpy(
							iface.m_adpcmB.data() + startAddr,
							&m_data[dataPos + 8],
							payloadSize);
					}
				}
				else if (blockType == 0x82 && blockSize > 8)
				{
					// YM2608 ADPCM-A（リズム音源）ROM
					// フォーマット: [ROM size 4B][start addr 4B][data...]
					const uint32_t startAddr = readUint32(dataPos + 4);
					const uint32_t payloadSize = blockSize - 8;
					if (dataPos + 8 + payloadSize <= m_data.size())
					{
						const uint32_t endAddr = startAddr + payloadSize;
						if (endAddr > iface.m_adpcmA.size())
						{
							iface.m_adpcmA.resize(endAddr, 0);
						}
						std::memcpy(
							iface.m_adpcmA.data() + startAddr,
							&m_data[dataPos + 8],
							payloadSize);
					}
				}

				pos = pos + 7 + blockSize;
			}
			else if (cmd == 0x66) // End of data
			{
				break;
			}
			else if (cmd == 0x56 || cmd == 0x57 || cmd == 0xA6 || cmd == 0xA7)
			{
				pos += 3;
			}
			else if (cmd == 0x61)
			{
				pos += 3;
			}
			else if (cmd == 0x62 || cmd == 0x63)
			{
				pos += 1;
			}
			else if (cmd >= 0x70 && cmd <= 0x7F)
			{
				pos += 1;
			}
			else if (cmd >= 0x30 && cmd <= 0x3F)
			{
				pos += 2;
			}
			else if (cmd == 0x4F || cmd == 0x50)
			{
				pos += 2;
			}
			else if (cmd >= 0x40 && cmd <= 0x4E)
			{
				pos += 3;
			}
			else if ((cmd >= 0x51 && cmd <= 0x5F) || (cmd >= 0xA0 && cmd <= 0xBF))
			{
				pos += 3;
			}
			else if (cmd >= 0xC0 && cmd <= 0xDF)
			{
				pos += 4;
			}
			else if (cmd >= 0xE0 && cmd <= 0xFF)
			{
				pos += 5;
			}
			else
			{
				pos += 1;
			}
		}
	}

	std::vector<uint8_t> m_data;        ///< VGMファイルデータ
	std::size_t m_dataOffset = 0;       ///< VGMデータ開始オフセット
	std::size_t m_loopOffset = 0;       ///< ループポイントのオフセット（0=ループなし）
	uint32_t m_totalSamples = 0;        ///< 総サンプル数（44100Hz基準）
	uint32_t m_ym2608Clock = OPNA_CLOCK;///< YM2608クロック値
	std::string m_title;                ///< GD3タグから取得したタイトル
};

} // namespace mitiru_mml
