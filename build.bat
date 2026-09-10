@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo Building Hostage - Single-File Portable Hosts Manager
echo =======================================================

:: Locate vcvars64.bat or vcvars32.bat
set "VCVARS="
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
) else (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

if not defined VCVARS (
    echo [ERROR] Visual Studio C++ build environment not found.
    exit /b 1
)

echo Calling !VCVARS!...
call "!VCVARS!" >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Failed to initialize MSVC environment.
    exit /b %errorlevel%
)

:: Create build output directory if not exists
if not exist "bin" mkdir "bin"

echo Compiling Windows Resources (app.rc)...
rc.exe /nologo /fo bin\app.res resources\app.rc
if %errorlevel% neq 0 (
    echo [ERROR] Resource compilation failed.
    exit /b %errorlevel%
)

echo Compiling and Linking Hostage.exe (/MT Static CRT, /O2 Optimization)...
cl.exe /nologo /O2 /GL /Gy /MT /EHsc /std:c++17 /W3 ^
    /I src /I resources ^
    /Fo:bin\ ^
    src\main.cpp src\hosts_manager.cpp src\win_util.cpp ^
    /link bin\app.res ^
    /OUT:bin\Hostage.exe ^
    /SUBSYSTEM:WINDOWS,6.01 ^
    /LTCG /OPT:REF /OPT:ICF ^
    gdiplus.lib comctl32.lib dnsapi.lib advapi32.lib shell32.lib user32.lib gdi32.lib

if %errorlevel% neq 0 (
    echo [ERROR] Compilation failed.
    exit /b %errorlevel%
)

echo.
echo =======================================================
echo [SUCCESS] Hostage.exe built successfully!
echo Executable: bin\Hostage.exe
echo =======================================================

:: Display file size
for %%A in ("bin\Hostage.exe") do (
    echo File size: %%~zA bytes (approx. %%~zA / 1024 KB)
)

:: Copy Hostage.exe to root directory for convenient direct access
copy /Y bin\Hostage.exe .\Hostage.exe >nul
echo Standalone single-file available at: .\Hostage.exe
