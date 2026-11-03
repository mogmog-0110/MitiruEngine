#include <mitiru/video/MfSource.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>

#include <mitiru/video/Nv12.hpp>

namespace mitiru::video
{

namespace
{

using Microsoft::WRL::ComPtr;

std::atomic<bool> g_forceStartupFailure{false};

std::string hex(HRESULT hr)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
	return buf;
}

/// Media Foundation の開始は process で数える。最後の source が閉じたときに止める
class MfRuntime
{
public:
	[[nodiscard]] static bool acquire(std::string* error)
	{
		std::lock_guard<std::mutex> lock(mutex());
		if (g_forceStartupFailure.load())
		{
			if (error != nullptr) { *error = "Media Foundation を開始できません (N エディションの Windows は Media Feature Pack を入れてください)"; }
			return false;
		}
		if (count() == 0)
		{
			const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
			if (FAILED(hr))
			{
				if (error != nullptr)
				{
					*error = "Media Foundation を開始できません (" + hex(hr) + "。N エディションの Windows は Media Feature Pack を入れてください)";
				}
				return false;
			}
		}
		++count();
		return true;
	}

	static void release() noexcept
	{
		std::lock_guard<std::mutex> lock(mutex());
		if (count() > 0 && --count() == 0) { MFShutdown(); }
	}

private:
	static std::mutex& mutex() { static std::mutex m; return m; }
	static int& count() { static int c = 0; return c; }
};

/// 復号は VideoStream の専用 thread が行う。その thread でも Media Foundation の部品が COM を使えるよう、thread ごとに 1 度だけ初期化する
void ensureComOnThisThread() noexcept
{
	struct Scope
	{
		bool owns = false;
		Scope() { owns = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
		~Scope() { if (owns) { CoUninitialize(); } }
	};
	thread_local const Scope scope;
	(void)scope;
}

class MfSource final : public IVideoSource
{
public:
	~MfSource() override
	{
		m_reader.Reset();
		m_byteStream.Reset();
		m_stream.Reset();
		if (m_runtime) { MfRuntime::release(); }
	}

	bool open(std::vector<std::uint8_t>&& bytes, std::string* error)
	{
		ensureComOnThisThread();
		if (bytes.empty()) { return fail(error, "ファイルが空です"); }
		if (!MfRuntime::acquire(error)) { return false; }
		m_runtime = true;

		// SHCreateMemStream は bytes を複製するので、渡した後は手放せる
		m_stream.Attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
		if (!m_stream) { return fail(error, "メモリのストリームを作れません"); }
		HRESULT hr = MFCreateMFByteStreamOnStream(m_stream.Get(), &m_byteStream);
		if (FAILED(hr)) { return failHr(error, "バイトストリームを作れません", hr); }
		ComPtr<IMFAttributes> streamAttrs;
		if (SUCCEEDED(m_byteStream.As(&streamAttrs))) { streamAttrs->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"video/mp4"); }

		// DXVA は使わない: GPU の無い環境でも、GPU が違っても、同じ復号のソフトウェアの経路を通す
		ComPtr<IMFAttributes> attrs;
		MFCreateAttributes(&attrs, 1);
		if (attrs) { attrs->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE); }
		hr = MFCreateSourceReaderFromByteStream(m_byteStream.Get(), attrs.Get(), &m_reader);
		if (FAILED(hr)) { return failHr(error, "MP4 として読めません (H.264 の MP4 を使ってください)", hr); }

		m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
		hr = m_reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
		if (FAILED(hr)) { return fail(error, "映像が見つかりません"); }

		ComPtr<IMFMediaType> want;
		MFCreateMediaType(&want);
		want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		want->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
		hr = m_reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, want.Get());
		if (FAILED(hr)) { return failHr(error, "映像のデコーダがありません (H.264 の MP4 を使ってください)", hr); }
		if (!readOutputType(error)) { return false; }
		if (!(m_info.fps > 0.0)) { return fail(error, "フレームレートが読めません (可変フレームレートは使えません)"); }
		if (m_info.width <= 0 || m_info.height <= 0) { return fail(error, "映像の大きさが読めません"); }
		if ((m_info.width | m_info.height | m_cropX | m_cropY) & 1) { return fail(error, "映像の幅と高さは偶数にしてください"); }
		return true;
	}

	[[nodiscard]] const VideoInfo& info() const noexcept override { return m_info; }

	bool decodeNext(std::uint8_t* rgba) override
	{
		ensureComOnThisThread();
		// 映像だけを選んであるので、サンプル無しの戻り (STREAMTICK など) は数回までなら読み直す
		for (int attempt = 0; attempt < 8; ++attempt)
		{
			DWORD flags = 0;
			ComPtr<IMFSample> sample;
			const HRESULT hr = m_reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, nullptr, &sample);
			if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR))) { return false; }
			if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
			{
				std::string ignored;
				if (!readOutputType(&ignored)) { return false; }
			}
			if (!sample) { continue; }
			if (rgba != nullptr && !convert(sample.Get(), rgba)) { return false; }
			return true;
		}
		return false;
	}

	void rewind() override
	{
		ensureComOnThisThread();
		PROPVARIANT start;
		PropVariantInit(&start);
		start.vt = VT_I8;
		start.hVal.QuadPart = 0;
		m_reader->SetCurrentPosition(GUID_NULL, start);
		PropVariantClear(&start);
	}

private:
	static bool fail(std::string* error, const char* why)
	{
		if (error != nullptr) { *error = why; }
		return false;
	}

	static bool failHr(std::string* error, const char* why, HRESULT hr)
	{
		if (error != nullptr) { *error = std::string(why) + " (" + hex(hr) + ")"; }
		return false;
	}

	/// 出力の型 (NV12) から大きさ、切り出し、行列、範囲、fps を読む。デコーダは途中で型を替えることがある
	bool readOutputType(std::string* error)
	{
		ComPtr<IMFMediaType> type;
		const HRESULT hr = m_reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &type);
		if (FAILED(hr)) { return failHr(error, "映像の型を読めません", hr); }
		UINT32 w = 0, h = 0;
		MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
		m_cropX = m_cropY = 0;
		MFVideoArea area{};
		if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), nullptr)) &&
		    area.Area.cx > 0 && area.Area.cy > 0)
		{
			m_cropX = static_cast<int>(area.OffsetX.value);
			m_cropY = static_cast<int>(area.OffsetY.value);
			w = static_cast<UINT32>(area.Area.cx);
			h = static_cast<UINT32>(area.Area.cy);
		}
		m_info.width = static_cast<int>(w);
		m_info.height = static_cast<int>(h);
		UINT32 num = 0, den = 0;
		if (SUCCEEDED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &num, &den)) && num > 0 && den > 0)
		{
			m_info.fps = static_cast<double>(num) / static_cast<double>(den);
		}
		UINT32 stride = 0;
		m_defaultStride = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? static_cast<std::int32_t>(stride) : 0;
		if (m_defaultStride < 0) { m_defaultStride = -m_defaultStride; }

		UINT32 matrix = 0, range = 0;
		type->GetUINT32(MF_MT_YUV_MATRIX, &matrix);
		type->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, &range);
		m_matrix = matrix == MFVideoTransferMatrix_BT709   ? YuvMatrix::Bt709
		         : matrix == MFVideoTransferMatrix_BT601   ? YuvMatrix::Bt601
		                                                   : defaultMatrixFor(m_info.height);
		m_fullRange = range == MFNominalRange_0_255;
		return true;
	}

	bool convert(IMFSample* sample, std::uint8_t* rgba)
	{
		ComPtr<IMFMediaBuffer> buffer;
		if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) { return false; }
		BYTE* scan0 = nullptr;
		LONG pitch = 0;
		DWORD total = 0;
		ComPtr<IMF2DBuffer> buffer2d;
		bool locked2d = false;
		bool locked = false;
		if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch)))
		{
			locked2d = true;
			buffer2d->GetContiguousLength(&total);
		}
		else
		{
			if (FAILED(buffer->Lock(&scan0, nullptr, &total))) { return false; }
			locked = true;
			pitch = m_defaultStride > 0 ? m_defaultStride : m_info.width;
		}
		const bool ok = convertLocked(scan0, pitch < 0 ? -pitch : pitch, total, rgba);
		if (locked2d) { buffer2d->Unlock2D(); }
		if (locked) { buffer->Unlock(); }
		return ok;
	}

	/// 輝度の面は高さ分の行、その直後に色差の面。デコーダは高さを 16 などへ切り上げた面を作るので、全体の長さから面の高さを割り出す
	bool convertLocked(const std::uint8_t* scan0, LONG pitch, DWORD total, std::uint8_t* rgba) const
	{
		if (scan0 == nullptr || pitch <= 0) { return false; }
		const std::size_t stride = static_cast<std::size_t>(pitch);
		const std::size_t needRows = static_cast<std::size_t>(m_cropY + m_info.height);
		if (total != 0 && total < stride * (needRows + needRows / 2)) { return false; }
		const std::size_t planeRows = std::max(needRows, (static_cast<std::size_t>(total) * 2) / (stride * 3));
		Nv12Layout layout;
		layout.y = scan0 + static_cast<std::size_t>(m_cropY) * stride + static_cast<std::size_t>(m_cropX);
		layout.uv = scan0 + stride * planeRows + static_cast<std::size_t>(m_cropY / 2) * stride + static_cast<std::size_t>(m_cropX);
		layout.yStride = stride;
		layout.uvStride = stride;
		nv12ToRgba(layout, m_info.width, m_info.height, m_matrix, m_fullRange, rgba);
		return true;
	}

	ComPtr<IStream>         m_stream;
	ComPtr<IMFByteStream>   m_byteStream;
	ComPtr<IMFSourceReader> m_reader;
	VideoInfo               m_info;
	int                     m_cropX = 0;
	int                     m_cropY = 0;
	std::int32_t            m_defaultStride = 0;
	YuvMatrix               m_matrix = YuvMatrix::Bt709;
	bool                    m_fullRange = false;
	bool                    m_runtime = false;
};

}  // namespace

std::unique_ptr<IVideoSource> openMfH264(std::vector<std::uint8_t>&& bytes, std::string* error)
{
	auto source = std::make_unique<MfSource>();
	if (!source->open(std::move(bytes), error)) { return nullptr; }
	return source;
}

void mfForceStartupFailure(bool on) noexcept
{
	g_forceStartupFailure.store(on);
}

}  // namespace mitiru::video
