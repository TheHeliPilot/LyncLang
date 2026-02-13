# Lync Compiler Installer for Windows
# Installs lync.exe to C:\Program Files\Lync and adds it to system PATH

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Lync Compiler Installer v0.2.0" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check if running as administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "ERROR: This installer must be run as Administrator!" -ForegroundColor Red
    Write-Host "Right-click PowerShell and select 'Run as Administrator', then run this script again." -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit 1
}

# Define installation directory
$installDir = "C:\Program Files\Lync"
$exePath = Join-Path $installDir "lync.exe"
$sourceExe = ".\cmake-build-debug\lync.exe"

# Check if source executable exists
if (-not (Test-Path $sourceExe)) {
    Write-Host "ERROR: Could not find lync.exe at $sourceExe" -ForegroundColor Red
    Write-Host "Please build the compiler first using:" -ForegroundColor Yellow
    Write-Host "  cmake --build cmake-build-debug" -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "Installation Directory: $installDir" -ForegroundColor Green
Write-Host "Source Executable: $sourceExe" -ForegroundColor Green
Write-Host ""

# Create installation directory if it doesn't exist
if (-not (Test-Path $installDir)) {
    Write-Host "Creating installation directory..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
} else {
    Write-Host "Installation directory already exists." -ForegroundColor Yellow
}

# Copy executable
Write-Host "Copying lync.exe to $installDir..." -ForegroundColor Yellow
Copy-Item $sourceExe $exePath -Force

if (-not (Test-Path $exePath)) {
    Write-Host "ERROR: Failed to copy executable!" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "Successfully copied lync.exe" -ForegroundColor Green
Write-Host ""

# Add to system PATH
Write-Host "Adding Lync to system PATH..." -ForegroundColor Yellow

# Get current system PATH
$currentPath = [Environment]::GetEnvironmentVariable("Path", "Machine")

# Check if already in PATH
if ($currentPath -like "*$installDir*") {
    Write-Host "Lync is already in system PATH." -ForegroundColor Yellow
} else {
    # Add to PATH
    $newPath = $currentPath + ";" + $installDir
    [Environment]::SetEnvironmentVariable("Path", $newPath, "Machine")
    Write-Host "Added $installDir to system PATH" -ForegroundColor Green
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Installation Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Lync compiler has been installed to: $installDir" -ForegroundColor White
Write-Host ""
Write-Host "IMPORTANT: You may need to restart your terminal or computer for PATH changes to take effect." -ForegroundColor Yellow
Write-Host ""
Write-Host "To verify installation, open a NEW terminal and run:" -ForegroundColor White
Write-Host "  lync --help" -ForegroundColor Cyan
Write-Host ""
Write-Host "To compile a Lync program:" -ForegroundColor White
Write-Host "  lync myfile.lync" -ForegroundColor Cyan
Write-Host ""
Write-Host "To compile and run:" -ForegroundColor White
Write-Host "  lync run myfile.lync" -ForegroundColor Cyan
Write-Host ""

Read-Host "Press Enter to exit"
