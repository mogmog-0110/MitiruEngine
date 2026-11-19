#pragma once

/// @file SharedReadVfs.hpp
/// @brief 音声ファイルを開いている間も、ほかのプロセスが書き換えたり置き換えたりできる miniaudio の VFS (Windows)
/// @details miniaudio の既定の VFS は Windows で CreateFileA を FILE_SHARE_READ だけで呼ぶので、BGM のように開いたままの
///          ファイルは、エディタの保存も DAW の書き出しも拒まれる。--watch-assets の間だけこれを使い、差し替えを host が見つけて鳴らし直す。

#include <atomic>

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

// miniaudio の既定の VFS が Win32 の HANDLE で読む構成 (miniaudio.h の MA_USE_WIN32_FILEIO と同じ条件)。そうでない構成では使わない
#if defined(MA_WIN32) && defined(MA_WIN32_DESKTOP) && !defined(MA_NO_WIN32_FILEIO) && !defined(MA_POSIX)
#define MITIRU_SHARED_READ_VFS 1
#endif

namespace mitiru::audio::detail
{

/// 次に作る MiniaudioEngine が共有の VFS を使うか。--watch-assets の起動が、engine を作る前に true にする
[[nodiscard]] inline std::atomic<bool>& sharedReadRequested() noexcept
{
	static std::atomic<bool> requested{false};
	return requested;
}

#ifdef MITIRU_SHARED_READ_VFS

class SharedReadVfs
{
public:
	/// engine が生きている間は消えない (resource manager が VFS を持ち続ける)
	[[nodiscard]] static ma_vfs* instance()
	{
		static SharedReadVfs vfs;
		return static_cast<ma_vfs*>(&vfs);
	}

private:
	SharedReadVfs()
	{
		(void)ma_default_vfs_init(&m_base, nullptr);
		primeDefaultVfs();
		m_callbacks.onOpen  = &onOpen;
		m_callbacks.onOpenW = &onOpenW;
		m_callbacks.onClose = &onClose;
		m_callbacks.onRead  = &onRead;
		m_callbacks.onWrite = &onWrite;
		m_callbacks.onSeek  = &onSeek;
		m_callbacks.onTell  = &onTell;
		m_callbacks.onInfo  = &onInfo;
	}

	/// 既定の VFS は、最初に開くときに SetFilePointerEx などを読み込む。自前で HANDLE を作る前に、自分の exe を 1 度開いて読み込ませる
	void primeDefaultVfs()
	{
		wchar_t exe[MAX_PATH];
		if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) { return; }
		ma_vfs_file file = nullptr;
		if (ma_vfs_open_w(static_cast<ma_vfs*>(&m_base), exe, MA_OPEN_MODE_READ, &file) == MA_SUCCESS)
		{
			(void)ma_vfs_close(static_cast<ma_vfs*>(&m_base), file);
		}
	}

	static SharedReadVfs& self(ma_vfs* vfs) { return *static_cast<SharedReadVfs*>(vfs); }
	static ma_vfs* base(ma_vfs* vfs) { return static_cast<ma_vfs*>(&self(vfs).m_base); }

	static ma_result openShared(const wchar_t* path, ma_vfs_file* file)
	{
		const HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE)
		{
			const DWORD err = GetLastError();
			return (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) ? MA_DOES_NOT_EXIST : MA_ACCESS_DENIED;
		}
		*file = h;   // 読み、位置、大きさ、閉じるは、既定の VFS が同じ HANDLE で行う
		return MA_SUCCESS;
	}

	static ma_result onOpenW(ma_vfs* vfs, const wchar_t* path, ma_uint32 mode, ma_vfs_file* file)
	{
		if ((mode & MA_OPEN_MODE_WRITE) != 0) { return ma_vfs_open_w(base(vfs), path, mode, file); }
		return openShared(path, file);
	}

	static ma_result onOpen(ma_vfs* vfs, const char* path, ma_uint32 mode, ma_vfs_file* file)
	{
		if ((mode & MA_OPEN_MODE_WRITE) != 0) { return ma_vfs_open(base(vfs), path, mode, file); }
		wchar_t wide[2048];
		if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 2048) == 0) { return ma_vfs_open(base(vfs), path, mode, file); }
		return openShared(wide, file);
	}

	static ma_result onClose(ma_vfs* vfs, ma_vfs_file file) { return ma_vfs_close(base(vfs), file); }
	static ma_result onRead(ma_vfs* vfs, ma_vfs_file file, void* dst, size_t size, size_t* read)
	{
		return ma_vfs_read(base(vfs), file, dst, size, read);
	}
	static ma_result onWrite(ma_vfs* vfs, ma_vfs_file file, const void* src, size_t size, size_t* written)
	{
		return ma_vfs_write(base(vfs), file, src, size, written);
	}
	static ma_result onSeek(ma_vfs* vfs, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
	{
		return ma_vfs_seek(base(vfs), file, offset, origin);
	}
	static ma_result onTell(ma_vfs* vfs, ma_vfs_file file, ma_int64* cursor)
	{
		return ma_vfs_tell(base(vfs), file, cursor);
	}
	static ma_result onInfo(ma_vfs* vfs, ma_vfs_file file, ma_file_info* info)
	{
		return ma_vfs_info(base(vfs), file, info);
	}

	ma_vfs_callbacks m_callbacks{};   ///< 先頭に置く (ma_vfs はこの形のオブジェクトを指す)
	ma_default_vfs   m_base{};
};

#endif

/// 共有の VFS を使う設定のとき、engine の設定へ渡す VFS。使わないとき (または Windows 以外) は nullptr
[[nodiscard]] inline ma_vfs* sharedReadVfsForEngine()
{
#ifdef MITIRU_SHARED_READ_VFS
	return sharedReadRequested().load() ? SharedReadVfs::instance() : nullptr;
#else
	return nullptr;
#endif
}

}  // namespace mitiru::audio::detail
