# VcpkgPwshFix.cmake — GAME_REQUESTS #56
#
# 問題: vcpkg の applocal (post-link DLL deploy) は pwsh を使い、その path を
#   Z_VCPKG_PWSH_PATH / Z_VCPKG_POWERSHELL_PATH に **版固定の WindowsApps パス**
#   (.../Microsoft.PowerShell_<ver>_x64__8wekyb3d8bbwe/pwsh.exe) でキャッシュする。
#   pwsh が WindowsApps 自動更新で版を上げると旧パスが消えるが、CMake は set 済み
#   find_program キャッシュを再探索しないため stale のまま applocal が
#   「指定されたパスが見つかりません」で落ち、mitiru build/run が起動不能になる。
#
# 対策: 版非依存の App Execution Alias
#   (%LOCALAPPDATA%/Microsoft/WindowsApps/pwsh.exe — 常に現行 pwsh を指す) へ
#   未設定 or stale のときだけ固定/是正する。これで pwsh 更新による再発を断つ。
#
# 使い方:
#   - engine 自身: 本 repo の root CMakeLists が project() より前に include 済み。
#   - consumer game: vcpkg toolchain ロードより前に効かせる必要があるため、configure 時に
#       -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=<engine>/cmake/VcpkgPwshFix.cmake
#     を渡す (mitiru CLI が自動付与する想定。手動 cmake でも可)。
#   - 無効化したい場合は環境変数 MITIRU_NO_PWSH_FIX=1。

if(WIN32 AND NOT DEFINED ENV{MITIRU_NO_PWSH_FIX})
	set(_mitiru_pwsh_alias "$ENV{LOCALAPPDATA}/Microsoft/WindowsApps/pwsh.exe")
	if(EXISTS "${_mitiru_pwsh_alias}")
		foreach(_var Z_VCPKG_PWSH_PATH Z_VCPKG_POWERSHELL_PATH)
			# 未設定、または キャッシュ済みパスが実在しない (= pwsh 更新で stale) なら alias へ。
			if((NOT DEFINED ${_var}) OR (NOT EXISTS "${${_var}}"))
				set(${_var} "${_mitiru_pwsh_alias}" CACHE FILEPATH
					"pwsh path pinned to version-independent App Execution Alias (MitiruEngine #56)" FORCE)
				message(STATUS "MitiruEngine #56: pinned ${_var} -> ${_mitiru_pwsh_alias}")
			endif()
		endforeach()
	endif()
	unset(_mitiru_pwsh_alias)
endif()
