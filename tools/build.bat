@echo off
rem Release build with MSVC + the Ninja that ships with Visual Studio.
rem Ninja compiles the generated chunks in parallel; the VS generator would
rem compile them one at a time (the template sets no /MP).
rem   tools\build.bat [jobs] [abi]   (default: all logical cores) -> build\ssx3_recomp.exe
rem   "abi" builds build-abi\ with -DRECOMP_ABI_CHECK: every call checks that
rem   the callee preserved ebx/esi/edi and logs "[ABI] sub_XXXXXXXX" when not.
setlocal EnableDelayedExpansion
set JOBS=%1
if "%JOBS%"=="" set JOBS=%NUMBER_OF_PROCESSORS%
set DIR=build
set FLAGS=
if /i "%2"=="abi" (
    set DIR=build-abi
    set FLAGS=-DCMAKE_C_FLAGS=/DRECOMP_ABI_CHECK
)
rem Any Visual Studio 2022 edition (Community, Professional, Build Tools, ...)
rem with the C++ workload: vswhere finds it.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "!VSWHERE!" for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR set "VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community"
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" (
    echo Visual Studio with the C++ workload not found ^(looked in !VSDIR!^) 1>&2
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
rem The Vulkan renderer (src/vk, RECOMP_GPU=vk) needs the LunarG SDK
rem (winget install KhronosGroup.VulkanSDK). A shell started before the
rem install does not see VULKAN_SDK yet: take the newest C:\VulkanSDK\*.
if not defined VULKAN_SDK for /d %%d in (C:\VulkanSDK\*) do set "VULKAN_SDK=%%d"
if defined VULKAN_SDK set "PATH=%VULKAN_SDK%\Bin;%PATH%"
cd /d "%~dp0.."
if not exist %DIR%\build.ninja (
    cmake -S . -B %DIR% -G Ninja -DCMAKE_BUILD_TYPE=Release %FLAGS% || exit /b 1
)
cmake --build %DIR% --parallel %JOBS%
exit /b %errorlevel%
