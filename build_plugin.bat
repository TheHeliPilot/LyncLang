@echo off
echo Building Lync VS Code Plugin...
cd ..\CMMEditor

echo Compiling TypeScript...
call npm run compile
if %errorLevel% neq 0 (
    echo Compilation failed!
    exit /b %errorLevel%
)

echo Packaging VSIX...
call npx vsce package --no-yarn
if %errorLevel% neq 0 (
    echo Packaging failed!
    exit /b %errorLevel%
)

echo Installing Extension...
call code --install-extension lync-language-0.3.2.vsix --force
if %errorLevel% neq 0 (
    echo Installation failed!
    exit /b %errorLevel%
)

echo Successfully installed version 0.3.2!
pause
