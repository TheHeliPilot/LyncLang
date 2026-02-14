@echo off
REM Lync Compiler Installer for Windows
REM Installs lync.exe to C:\Program Files\Lync and adds it to system PATH

echo ========================================
echo Lync Compiler Installer v0.3.2
echo ========================================
echo.

REM Check if running as administrator
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This installer must be run as Administrator!
    echo Right-click this file and select "Run as Administrator"
    echo.
    pause
    exit /b 1
)

REM Get the directory where this .bat file is located
set "SCRIPT_DIR=%~dp0"
set "INSTALL_DIR=C:\Program Files\Lync"
set "SOURCE_EXE=%SCRIPT_DIR%lync.exe"

REM Check if source executable exists
if not exist "%SOURCE_EXE%" (
    echo ERROR: Could not find lync.exe in the same directory as this installer
    echo Expected location: %SOURCE_EXE%
    echo.
    echo Please ensure lync.exe is in the same folder as install.bat
    echo.
    pause
    exit /b 1
)

echo Installation Directory: %INSTALL_DIR%
echo Source Executable: %SOURCE_EXE%
echo.

REM Create installation directory if it doesn't exist
if not exist "%INSTALL_DIR%" (
    echo Creating installation directory...
    mkdir "%INSTALL_DIR%"
) else (
    echo Installation directory already exists.
)

REM Copy executable
echo Copying lync.exe to %INSTALL_DIR%...
copy /Y "%SOURCE_EXE%" "%INSTALL_DIR%\lync.exe" >nul

if not exist "%INSTALL_DIR%\lync.exe" (
    echo ERROR: Failed to copy executable!
    pause
    exit /b 1
)

echo Successfully copied lync.exe
echo.

REM Add to system PATH
echo Adding Lync to system PATH...

REM Check if already in PATH
echo %PATH% | findstr /C:"%INSTALL_DIR%" >nul
if %errorLevel% equ 0 (
    echo Lync is already in system PATH.
) else (
    REM Read current system PATH from registry (avoids truncation)
    for /f "tokens=2*" %%A in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v Path 2^>nul') do set "SYS_PATH=%%B"
    setx /M PATH "%SYS_PATH%;%INSTALL_DIR%" >nul
    echo Added %INSTALL_DIR% to system PATH
)

echo.
echo ========================================
echo Installation Complete!
echo ========================================
echo.
echo Lync compiler has been installed to: %INSTALL_DIR%
echo.
echo IMPORTANT: You may need to restart your terminal or computer
echo for PATH changes to take effect.
echo.
echo To verify installation, open a NEW terminal and run:
echo   lync --help
echo.
echo To compile a Lync program:
echo   lync myfile.lync
echo.
echo To compile and run:
echo   lync run myfile.lync
echo.
pause
