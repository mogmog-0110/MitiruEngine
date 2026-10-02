# VcpkgPwshFix.cmake、GAME_REQUESTS #56
#
# 問題: vcpkg の applocal (リンク後の DLL 配置) は pwsh を使い、そのパスを
#   Z_VCPKG_PWSH_PATH / Z_VCPKG_POWERSHELL_PATH に **バージョンが固定された WindowsApps パス**
#   (.../Microsoft.PowerShell_<ver>_x64__8wekyb3d8bbwe/pwsh.exe) としてキャッシュする。
#   pwsh が WindowsApps の自動更新でバージョンアップすると古いパスが消えるが、CMake は set 済みの
#   find_program キャッシュを再探索しないため、古いパスが applocal に残り、
#   「指定されたパスが見つかりません」というエラーで失敗して、mitiru build/run が起動できなくなる。
#
# 対策: バージョンに依存しない App Execution Alias
#   (%LOCALAPPDATA%/Microsoft/WindowsApps/pwsh.exe。常に現在の pwsh を指す) に、
#   未設定または古い場合だけ設定または修正する。これで pwsh の更新による再発を防ぐ。
#
# 使い方:
#   - engine 自身: 本リポジトリのルート CMakeLists では project() より前に include 済み。
#   - consumer game: vcpkg toolchain の読み込みより前に適用する必要があるため、configure 時に
#       -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=<engine>/cmake/VcpkgPwshFix.cmake
#     を渡す (mitiru CLI が自動で付与する想定。手動の cmake でも可)。
#   - 無効にしたい場合は環境変数 MITIRU_NO_PWSH_FIX=1 を設定する。

if(WIN32 AND NOT DEFINED ENV{MITIRU_NO_PWSH_FIX})
	set(_mitiru_pwsh_alias "$ENV{LOCALAPPDATA}/Microsoft/WindowsApps/pwsh.exe")
	if(EXISTS "${_mitiru_pwsh_alias}")
		foreach(_var Z_VCPKG_PWSH_PATH Z_VCPKG_POWERSHELL_PATH)
			# 未設定、またはキャッシュ済みのパスが実在しない (= pwsh の更新で古くなっている) 場合は alias を設定する。
			if((NOT DEFINED ${_var}) OR (NOT EXISTS "${${_var}}"))
				set(${_var} "${_mitiru_pwsh_alias}" CACHE FILEPATH
					"pwsh path pinned to version-independent App Execution Alias (MitiruEngine #56)" FORCE)
				message(STATUS "MitiruEngine #56: pinned ${_var} -> ${_mitiru_pwsh_alias}")
			endif()
		endforeach()
	endif()
	unset(_mitiru_pwsh_alias)
endif()
