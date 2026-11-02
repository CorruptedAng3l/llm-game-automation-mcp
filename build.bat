@echo off
rem Builds vinput for x64 and x86 with MSVC (Visual Studio 2022 or newer, or the Build Tools, with the C++ x64/x86 tools).
rem   dist\vinput.exe     dist\vinput32.exe      CLI: inject / launch / control (vinput.exe hands 32-bit targets to vinput32.exe)
rem   dist\vinput64.dll   dist\vinput32.dll      the injected DLLs
rem   dist\vinput_x64.lib dist\vinput_x86.lib    static libs for compiling vinput INTO your own DLL (VINPUT_COMPILE_IN)
rem   dist\vi_*.exe, dist\compilein_test*.dll, dist\acdummy\    synthetic test targets used by verify\*.ps1
rem MSVC is found through %VINPUT_VCVARS_DIR% (a ...\VC\Auxiliary\Build folder) or, if unset, vswhere.
setlocal
if "%~1"==":arch" goto :arch
call "%~f0" :arch x64 || goto :fail
call "%~f0" :arch x86 || goto :fail
echo BUILD OK
exit /b 0
:fail
echo BUILD FAILED
exit /b 1

:arch
setlocal
set ARCH=%~2
set ROOT=%~dp0
set TEMP=%ROOT%build\temp
set TMP=%TEMP%
if not exist "%TEMP%" mkdir "%TEMP%"
set MH=%ROOT%third_party\minhook
set VCDIR=%VINPUT_VCVARS_DIR%
if not defined VCDIR call :findvs
if not defined VCDIR (echo [%ARCH%] MSVC not found: install Visual Studio with C++ tools or set VINPUT_VCVARS_DIR & exit /b 1)
if "%ARCH%"=="x64" (set VC=vcvars64.bat& set HDE=hde64& set DLL=vinput64.dll& set EXE=vinput.exe) else (set VC=vcvars32.bat& set HDE=hde32& set DLL=vinput32.dll& set EXE=vinput32.exe)
call "%VCDIR%\%VC%" >nul || exit /b 1
if not exist "%ROOT%build\%ARCH%" mkdir "%ROOT%build\%ARCH%"
if not exist "%ROOT%dist" mkdir "%ROOT%dist"
pushd "%ROOT%build\%ARCH%"
set CF=/nologo /O2 /MT /EHsc /W3 /Zi /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /I"%MH%\include" /I"%MH%\src" /I"%ROOT%src"
cl %CF% /FI"%ROOT%src\vi_mh_rename.h" /c "%MH%\src\buffer.c" /Fovimh_buffer.obj || goto :afail
cl %CF% /FI"%ROOT%src\vi_mh_rename.h" /c "%MH%\src\hook.c" /Fovimh_hook.obj || goto :afail
cl %CF% /FI"%ROOT%src\vi_mh_rename.h" /c "%MH%\src\trampoline.c" /Fovimh_trampoline.obj || goto :afail
cl %CF% /FI"%ROOT%src\vi_mh_rename.h" /c "%MH%\src\hde\%HDE%.c" /Fovimh_hde.obj || goto :afail
cl %CF% /std:c++17 /c "%ROOT%src\vinput_core.cpp" "%ROOT%src\vinput_dll.cpp" || goto :afail
lib /nologo /OUT:vinput_%ARCH%.lib vinput_core.obj vimh_buffer.obj vimh_hook.obj vimh_trampoline.obj vimh_hde.obj || goto :afail
link /nologo /DLL /DEBUG /OUT:%DLL% vinput_dll.obj vinput_%ARCH%.lib || goto :afail
cl %CF% /std:c++20 "%ROOT%src\vinput_cli.cpp" /Fe:%EXE% /link /SUBSYSTEM:CONSOLE || goto :afail
rem vinput.exe injects a per-build copy (dist\run\...), so dist\%DLL% is only locked if something LoadLibrary'd it directly
copy /y %DLL% "%ROOT%dist\" >nul || (echo [%ARCH%] dist\%DLL% is locked by a running process - stop it or inject from build\%ARCH% & goto :afail)
copy /y %EXE% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%EXE% is in use - NOT replaced (fresh copy in build\%ARCH%\%EXE%)
copy /y vinput_%ARCH%.lib "%ROOT%dist\" >nul || goto :afail
if exist %DLL:.dll=.pdb% copy /y %DLL:.dll=.pdb% "%ROOT%dist\" >nul
rem --- tests: positive-control target (both arches) and a compiled-in host DLL ---
if "%ARCH%"=="x64" (set TT=vi_testinput.exe& set CI=compilein_test64.dll) else (set TT=vi_testinput32.exe& set CI=compilein_test32.dll)
cl %CF% /std:c++17 "%ROOT%test\vi_testinput.cpp" /Fe:%TT% /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib dinput8.lib dxguid.lib || goto :afail
copy /y %TT% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%TT% in use - not replaced
if "%ARCH%"=="x64" (set CP=vi_channelprobe.exe) else (set CP=vi_channelprobe32.exe)
cl %CF% /std:c++17 "%ROOT%test\vi_channelprobe.cpp" /Fe:%CP% /link /SUBSYSTEM:WINDOWS || goto :afail
copy /y %CP% "%ROOT%dist\" >nul || goto :afail
rem --- native-capture synthetic targets (known frame-counter + colour-bar pattern, test\vi_pattern.h) ---
if "%ARCH%"=="x64" (set D12=vi_d3d12test.exe) else (set D12=vi_d3d12test32.exe)
cl %CF% /std:c++17 "%ROOT%test\vi_d3d12test.cpp" /Fe:%D12% /link /SUBSYSTEM:WINDOWS || goto :afail
copy /y %D12% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%D12% in use - not replaced
if "%ARCH%"=="x64" cl %CF% "%ROOT%test\vi_aclaunch.cpp" /Fe:vi_aclaunch.exe /link /SUBSYSTEM:CONSOLE || goto :afail
if "%ARCH%"=="x64" copy /y vi_aclaunch.exe "%ROOT%dist\" >nul
if "%ARCH%"=="x64" (set GLT=vi_gltest.exe) else (set GLT=vi_gltest32.exe)
cl %CF% /std:c++17 "%ROOT%test\vi_gltest.cpp" /Fe:%GLT% /link /SUBSYSTEM:WINDOWS || goto :afail
copy /y %GLT% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%GLT% in use - not replaced
if "%ARCH%"=="x64" (set VKT=vi_vktest.exe) else (set VKT=vi_vktest32.exe)
cl %CF% /std:c++17 "%ROOT%test\vi_vktest.cpp" /Fe:%VKT% /link /SUBSYSTEM:WINDOWS || goto :afail
copy /y %VKT% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%VKT% in use - not replaced
if "%ARCH%"=="x64" (set XIT=vi_xinputtest.exe) else (set XIT=vi_xinputtest32.exe)
cl %CF% /std:c++17 "%ROOT%test\vi_xinputtest.cpp" /Fe:%XIT% /link /SUBSYSTEM:WINDOWS || goto :afail
copy /y %XIT% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%XIT% in use - not replaced
if "%ARCH%"=="x64" (set DI1=vi_dinput1test.exe) else (set DI1=vi_dinput1test32.exe)
cl %CF% /std:c++17 "%ROOT%test\vi_dinput1test.cpp" /Fe:%DI1% /link /SUBSYSTEM:WINDOWS || goto :afail
copy /y %DI1% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%DI1% in use - not replaced
if "%ARCH%"=="x64" (set UT=vi_uitarget.exe& set UU=vi_uitest.exe& set UN=vi_ncctest.exe) else (set UT=vi_uitarget32.exe& set UU=vi_uitest32.exe& set UN=vi_ncctest32.exe)
cl %CF% /std:c++17 "%ROOT%test\vi_uitarget.cpp" /Fe:%UT% /link /SUBSYSTEM:WINDOWS || goto :afail
copy /y %UT% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%UT% in use - not replaced
cl %CF% /std:c++17 "%ROOT%test\vi_uitest.cpp" /Fe:%UU% /link /SUBSYSTEM:CONSOLE || goto :afail
copy /y %UU% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%UU% in use - not replaced
cl %CF% /std:c++17 "%ROOT%test\vi_ncctest.cpp" /Fe:%UN% /link /SUBSYSTEM:CONSOLE || goto :afail
copy /y %UN% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%UN% in use - not replaced
if "%ARCH%"=="x64" (set OVT=vi_overlaytarget.exe) else (set OVT=vi_overlaytarget32.exe)
cl %CF% /std:c++17 "%ROOT%test\vi_overlaytarget.cpp" /Fe:%OVT% /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib || goto :afail
copy /y %OVT% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%OVT% in use - not replaced
rem --- anti-cheat refusal test: a plain target + an EMPTY dummy DLL that only has an anti-cheat-like NAME ---
if "%ARCH%"=="x64" (set ACT=vi_actarget.exe& set ACD=EasyAntiCheat_x64.dll) else (set ACT=vi_actarget32.exe& set ACD=BEClient.dll)
cl %CF% /std:c++17 "%ROOT%test\vi_actarget.cpp" /Fe:%ACT% /link /SUBSYSTEM:WINDOWS || goto :afail
copy /y %ACT% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%ACT% in use - not replaced
cl %CF% /LD "%ROOT%test\vi_acdummy.cpp" /Fe:%ACD% || goto :afail
if not exist "%ROOT%dist\acdummy\%ARCH%" mkdir "%ROOT%dist\acdummy\%ARCH%"
copy /y %ACD% "%ROOT%dist\acdummy\%ARCH%\" >nul || echo [%ARCH%] WARNING: dist\acdummy\%ARCH%\%ACD% in use - not replaced
if not exist host_mh mkdir host_mh
cl %CF% /c "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\%HDE%.c" /Fohost_mh\ || goto :afail
cl %CF% /std:c++17 /DVINPUT_COMPILE_IN /c "%ROOT%test\compilein_test.cpp" || goto :afail
link /nologo /DLL /OUT:%CI% compilein_test.obj host_mh\buffer.obj host_mh\hook.obj host_mh\trampoline.obj host_mh\%HDE%.obj vinput_%ARCH%.lib || goto :afail
copy /y %CI% "%ROOT%dist\" >nul || echo [%ARCH%] WARNING: dist\%CI% in use - not replaced
popd
echo [%ARCH%] OK
exit /b 0
:afail
popd
echo [%ARCH%] FAILED
exit /b 1

:findvs
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 0
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCDIR=%%i\VC\Auxiliary\Build"
exit /b 0
