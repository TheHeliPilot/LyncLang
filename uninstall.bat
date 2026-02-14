@echo off
REM Lync Compiler Uninstaller for Windows
REM Removes lync.exe from C:\Program Files\Lync and removes from system PATH

echo ========================================
echo Lync Compiler Uninstaller v0.3.0
echo ========================================
echo.

REM Check if running as administrator
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This uninstaller must be run as Administrator!
    echo Right-click this file and select "Run as Administrator"
    echo.
    pause
    exit /b 1
)

REM Define installation directory
set "INSTALL_DIR=C:\Program Files\Lync"

echo This will remove Lync compiler from your system.
echo Installation Directory: %INSTALL_DIR%
echo.
set /p CONFIRM="Are you sure you want to uninstall? (yes/no): "

if /i not "%CONFIRM%"=="yes" (
    echo Uninstall cancelled.
    pause
    exit /b 0
)

echo.

REM Remove installation directory
if exist "%INSTALL_DIR%" (
    echo Removing installation directory...
    rmdir /S /Q "%INSTALL_DIR%"
    echo Removed %INSTALL_DIR%
) else (
    echo Installation directory not found
)

REM Remove from system PATH automatically
echo Removing Lync from system PATH...
powershell -Command "$path = [Environment]::GetEnvironmentVariable('Path', 'Machine'); $newPath = ($path -split ';' | Where-Object { $_ -ne '%INSTALL_DIR%' }) -join ';'; [Environment]::SetEnvironmentVariable('Path', $newPath, 'Machine')" 2>nul
if %errorLevel% equ 0 (
    echo Removed %INSTALL_DIR% from system PATH
) else (
    echo NOTE: Could not automatically remove from PATH.
    echo Please manually remove "%INSTALL_DIR%" from your system PATH.
)
echo.

echo ========================================
echo Uninstall Complete!
echo ========================================
echo.
echo Lync compiler has been removed from your system.
echo You may need to restart your terminal for changes to take effect.
echo.
pause
