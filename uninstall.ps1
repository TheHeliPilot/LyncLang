# Lync Compiler Uninstaller for Windows
# Removes lync.exe from C:\Program Files\Lync and removes from system PATH

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Lync Compiler Uninstaller v0.2.0" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check if running as administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "ERROR: This uninstaller must be run as Administrator!" -ForegroundColor Red
    Write-Host "Right-click PowerShell and select 'Run as Administrator', then run this script again." -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit 1
}

# Define installation directory
$installDir = "C:\Program Files\Lync"

Write-Host "This will remove Lync compiler from your system." -ForegroundColor Yellow
Write-Host "Installation Directory: $installDir" -ForegroundColor White
Write-Host ""
$confirm = Read-Host "Are you sure you want to uninstall? (yes/no)"

if ($confirm -ne "yes") {
    Write-Host "Uninstall cancelled." -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 0
}

Write-Host ""

# Remove from system PATH
Write-Host "Removing Lync from system PATH..." -ForegroundColor Yellow

$currentPath = [Environment]::GetEnvironmentVariable("Path", "Machine")

if ($currentPath -like "*$installDir*") {
    $newPath = $currentPath -replace [regex]::Escape(";$installDir"), ""
    $newPath = $newPath -replace [regex]::Escape("$installDir;"), ""
    $newPath = $newPath -replace [regex]::Escape($installDir), ""
    [Environment]::SetEnvironmentVariable("Path", $newPath, "Machine")
    Write-Host "Removed from system PATH" -ForegroundColor Green
} else {
    Write-Host "Not found in system PATH" -ForegroundColor Yellow
}

# Remove installation directory
if (Test-Path $installDir) {
    Write-Host "Removing installation directory..." -ForegroundColor Yellow
    Remove-Item -Path $installDir -Recurse -Force
    Write-Host "Removed $installDir" -ForegroundColor Green
} else {
    Write-Host "Installation directory not found" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Uninstall Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Lync compiler has been removed from your system." -ForegroundColor White
Write-Host "You may need to restart your terminal for PATH changes to take effect." -ForegroundColor Yellow
Write-Host ""

Read-Host "Press Enter to exit"
