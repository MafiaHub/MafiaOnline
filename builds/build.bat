@echo off
setlocal
if "%~1"=="" goto usage
set "M1O_ARCH=%~2"
if not "%M1O_ARCH%"=="32" if not "%M1O_ARCH%"=="64" goto usage
set "M1O_TARGET=x64"
set "M1O_VCVARS=amd64"
set "M1O_SUFFIX=-64"
if "%M1O_ARCH%"=="32" (
    set "M1O_TARGET=x86"
    set "M1O_VCVARS=amd64_x86"
    set "M1O_SUFFIX=-32"
)
set "M1O_CONFIG=%~3"
if not defined M1O_CONFIG set "M1O_CONFIG=Debug"
set "M1O_BUILD=build%M1O_SUFFIX%"
if "%M1O_CONFIG%"=="Release" set "M1O_BUILD=build-prod%M1O_SUFFIX%"
if "%M1O_CONFIG%"=="RelWithDebInfo" set "M1O_BUILD=build-release%M1O_SUFFIX%"
if not "%M1O_CONFIG%"=="Debug" if not "%M1O_CONFIG%"=="Release" if not "%M1O_CONFIG%"=="RelWithDebInfo" goto usage
if not defined VSCMD_ARG_TGT_ARCH (
    call :setup_msvc
    if errorlevel 1 exit /b 1
)
if not "%VSCMD_ARG_TGT_ARCH%"=="%M1O_TARGET%" (
    echo An %M1O_TARGET% compiler environment is required; detected "%VSCMD_ARG_TGT_ARCH%".
    exit /b 1
)
pushd "%~dp0.."
chcp 65001 >nul
rem Where the Framework comes from (cmake/FrameworkBootstrap.cmake): the pinned SDK release by
rem default, FW_SDK_ROOT for a locally installed SDK, FW_SOURCE_DIR to build it from a checkout.
rem Both are cache entries, so switching modes clears the other one instead of letting a stale
rem value win in an existing build tree. The pinned archive is shared by the 32- and 64-bit trees
rem (it carries both architectures) unless FW_SDK_CACHE_DIR points elsewhere.
if not defined FW_SDK_CACHE_DIR set "FW_SDK_CACHE_DIR=%CD%/builds/framework-sdk"
rem The D3D8 renderer, launcher and asset streamer support Mafia1Online needs is not in a Framework
rem release yet. Checked out at code/projects/<name> inside a Framework tree that carries it, build
rem that tree from source unless another source was asked for.
if not defined FW_SDK_ROOT if not defined FW_SOURCE_DIR if exist "%CD%\..\..\..\code\framework\src\graphics\backend\d3d8.h" set "FW_SOURCE_DIR=%CD%\..\..\.."
set "M1O_FRAMEWORK_ARGS=-UFW_SDK_ROOT -UFW_SOURCE_DIR"
if defined FW_SDK_ROOT set "M1O_FRAMEWORK_ARGS=-DFW_SDK_ROOT=%FW_SDK_ROOT% -UFW_SOURCE_DIR"
if defined FW_SOURCE_DIR set "M1O_FRAMEWORK_ARGS=-DFW_SOURCE_DIR=%FW_SOURCE_DIR% -UFW_SDK_ROOT"
rem The web UI is prebuilt into resources/ui; the client target copies it next to the DLL.
if "%~1"=="Mafia1OnlineClient" call :build_ui
cmake -S . -B "builds\%M1O_BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%M1O_CONFIG% -DBUILD_SHARED_LIBS=OFF -DCMAKE_MAKE_PROGRAM="%CD%\scripts\ninja_utf8.cmd" -DCMAKE_PROJECT_INCLUDE_BEFORE="%CD%\cmake\MSVCUtf8.cmake" %M1O_FRAMEWORK_ARGS% %M1O_CMAKE_ARGS%
if errorlevel 1 goto failed
cmake --build "builds\%M1O_BUILD%" --target "%~1" --parallel %CMAKE_BUILD_PARALLEL_LEVEL%
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
:usage
echo Usage: builds\build.bat ^<target^> ^<32^|64^> [Debug^|Release^|RelWithDebInfo]
echo   FW_SDK_ROOT=^<dir^>    build against a locally installed Framework SDK
echo   FW_SOURCE_DIR=^<dir^>  build the Framework from a checkout as a subdirectory
exit /b 2
:build_ui
where npm >nul 2>nul
if errorlevel 1 (
    echo npm not found; the client falls back to quick join and the native chat.
    exit /b 0
)
set "npm_config_cache=%CD%\builds\.cache\npm"
if not exist "ui\node_modules\.package-lock.json" call npm --prefix ui ci --no-audit --no-fund
call npm --prefix ui run build
exit /b 0
:setup_msvc
set "M1O_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%M1O_VSWHERE%" (
    echo Visual Studio Build Tools not found. Use a developer prompt.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%M1O_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "M1O_VS_ROOT=%%i"
rem Preview and Insiders channels are hidden from vswhere unless asked for; use one only when no release install exists.
if not defined M1O_VS_ROOT for /f "usebackq tokens=*" %%i in (`"%M1O_VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "M1O_VS_ROOT=%%i"
if not defined M1O_VS_ROOT (
    echo No Visual Studio install with the C++ tools found. Use a developer prompt.
    exit /b 1
)
call "%M1O_VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" %M1O_VCVARS%
exit /b %errorlevel%
