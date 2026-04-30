@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem EFZ DirectDraw wrapper build script.
rem Usage:
rem   build.bat [Debug|Release] [install]
rem
rem Debug   : static debug CRT, debug info, logging enabled.
rem Release : static release CRT, optimized, logging disabled.

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "SRC=%SCRIPT_DIR%ddraw.c"
set "DEF=%SCRIPT_DIR%ddraw.def"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
if /I "%CONFIG%"=="install" (
    set "CONFIG=Debug"
    set "INSTALL=1"
) else (
    set "INSTALL=0"
)
if /I "%~2"=="install" set "INSTALL=1"

if /I "%CONFIG%"=="Debug" (
    set "LOGGING=1"
    set "MSVC_OPT=/Od /Zi /MTd"
    set "MSVC_LINK_DEBUG=/DEBUG"
    set "GCC_OPT=-O0 -g"
) else if /I "%CONFIG%"=="Release" (
    set "LOGGING=0"
    set "MSVC_OPT=/O2 /MT"
    set "MSVC_LINK_DEBUG="
    set "GCC_OPT=-O2"
) else (
    echo [ERROR] Unknown config "%CONFIG%". Use Debug or Release.
    exit /b 1
)

set "OUT_DIR=%SCRIPT_DIR%build\%CONFIG%"
set "OUT_DLL=%OUT_DIR%\ddraw.dll"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

echo.
echo === EFZ DirectDraw Wrapper: %CONFIG% ===
echo.

call :ensure_msvc
if not errorlevel 1 (
    echo [INFO] Using MSVC x86 with static CRT
    cl /nologo /W3 /GS- /wd4100 %MSVC_OPT% ^
       /DUNICODE /D_UNICODE ^
       /DDDRAW_WRAPPER_ENABLE_LOGGING=%LOGGING% ^
       /LD /Fe:"%OUT_DLL%" /Fo:"%OUT_DIR%\ddraw.obj" /Fd:"%OUT_DIR%\ddraw.pdb" ^
       "%SRC%" ^
       /link /DEF:"%DEF%" /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SUBSYSTEM:WINDOWS,6.0 ^
       /IMPLIB:"%OUT_DIR%\ddraw.lib" %MSVC_LINK_DEBUG% ^
       kernel32.lib user32.lib gdi32.lib
    if errorlevel 1 exit /b 1
    goto :success
)

where gcc >nul 2>&1
if not errorlevel 1 (
    echo [INFO] Using GCC/MinGW with static libgcc
    gcc -m32 %GCC_OPT% -Wall -Wno-unused-parameter ^
        -DUNICODE -D_UNICODE ^
        -DDDRAW_WRAPPER_ENABLE_LOGGING=%LOGGING% ^
        -shared -o "%OUT_DLL%" "%SRC%" "%DEF%" ^
        -static -static-libgcc ^
        -Wl,--kill-at -Wl,--enable-stdcall-fixup ^
        -lkernel32 -luser32 -lgdi32
    if errorlevel 1 exit /b 1
    goto :success
)

where i686-w64-mingw32-gcc >nul 2>&1
if not errorlevel 1 (
    echo [INFO] Using i686-w64-mingw32-gcc with static libgcc
    i686-w64-mingw32-gcc %GCC_OPT% -Wall -Wno-unused-parameter ^
        -DUNICODE -D_UNICODE ^
        -DDDRAW_WRAPPER_ENABLE_LOGGING=%LOGGING% ^
        -shared -o "%OUT_DLL%" "%SRC%" "%DEF%" ^
        -static -static-libgcc ^
        -Wl,--kill-at -Wl,--enable-stdcall-fixup ^
        -lkernel32 -luser32 -lgdi32
    if errorlevel 1 exit /b 1
    goto :success
)

echo [ERROR] No supported 32-bit C compiler found.
exit /b 1

:success
echo.
echo [OK] Built: %OUT_DLL%
copy /Y "%OUT_DLL%" "%ROOT_DIR%\ddraw.dll" >nul
echo [OK] Copied to: %ROOT_DIR%\ddraw.dll

if "%INSTALL%"=="1" (
    echo [INSTALL] Copying ddraw.dll to nearby game folders...
    for %%D in (
        "%ROOT_DIR%"
        "%ROOT_DIR%\..\EFZ 1.11"
        "%ROOT_DIR%\..\EFZ BME 3.03Beta"
        "%ROOT_DIR%\..\EFZ BSE 2.13"
        "%ROOT_DIR%\..\EFZ Memorial 4.00"
    ) do (
        if exist "%%~D" (
            copy /Y "%OUT_DLL%" "%%~D\ddraw.dll" >nul
            echo   %%~D
        )
    )
)
exit /b 0

:ensure_msvc
where cl >nul 2>&1
if not errorlevel 1 exit /b 0

set "VCVARS="
for %%V in (
    "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
) do (
    if exist "%%~V" (
        set "VCVARS=%%~V"
        goto :call_vcvars
    )
)
exit /b 1

:call_vcvars
call "%VCVARS%" x86 >nul
where cl >nul 2>&1
exit /b %errorlevel%
