# Installation Guide

This guide explains how to install the Lync compiler on your system so you can run it from anywhere using the `lync` command.

---

## Windows Installation

### Prerequisites

1. A C compiler (GCC, Clang, or MSVC) must be installed and available in your PATH
2. The Lync compiler must be built first

### Building the Compiler

If you haven't built the compiler yet:

```bash
cmake -B cmake-build-debug
cmake --build cmake-build-debug
```

This will create `lync.exe` in `cmake-build-debug\Debug\`.

### Option 1: PowerShell Installer (Recommended)

1. **Right-click PowerShell** and select **"Run as Administrator"**
2. Navigate to the Lync project directory:
   ```powershell
   cd C:\Users\bucka\CLionProjects\CMinusMinus
   ```
3. Run the installer:
   ```powershell
   .\install.ps1
   ```

The installer will:
- Copy `lync.exe` to `C:\Program Files\Lync\`
- Add `C:\Program Files\Lync\` to your system PATH
- Display success message

### Option 2: Batch File Installer

1. **Right-click** `install.bat` and select **"Run as Administrator"**
2. Follow the on-screen prompts

### Option 3: Manual Installation

If you prefer to install manually:

1. Create installation directory:
   ```bash
   mkdir "C:\Program Files\Lync"
   ```

2. Copy the executable:
   ```bash
   copy cmake-build-debug\Debug\lync.exe "C:\Program Files\Lync\lync.exe"
   ```

3. Add to PATH:
   - Press `Win + X` and select **"System"**
   - Click **"Advanced system settings"**
   - Click **"Environment Variables"**
   - Under **"System variables"**, select **"Path"** and click **"Edit"**
   - Click **"New"** and add: `C:\Program Files\Lync`
   - Click **"OK"** on all dialogs

4. Restart your terminal or computer

---

## Verifying Installation

After installation, **open a NEW terminal** (to load the updated PATH) and run:

```bash
lync --help
```

You should see the Lync compiler help message.

To verify it works, create a test file:

```bash
echo "def main(): int { print(\"Hello, Lync!\"); return 0; }" > hello.lync
lync run hello.lync
```

You should see: `Hello, Lync!`

---

## Usage After Installation

Once installed, you can use `lync` from any directory:

```bash
# Compile a program
lync myprogram.lync

# Compile and run
lync run myprogram.lync

# Specify output name
lync myprogram.lync -o myapp

# Keep intermediate C file
lync myprogram.lync --emit-c

# Enable optimizations
lync myprogram.lync -O2
```

---

## Uninstallation

### PowerShell Uninstaller

1. **Right-click PowerShell** and select **"Run as Administrator"**
2. Navigate to the Lync project directory
3. Run:
   ```powershell
   .\uninstall.ps1
   ```

### Batch File Uninstaller

1. **Right-click** `uninstall.bat` and select **"Run as Administrator"**
2. Follow the prompts

### Manual Uninstallation

1. Delete the installation directory:
   ```bash
   rmdir /S /Q "C:\Program Files\Lync"
   ```

2. Remove from PATH:
   - Press `Win + X` and select **"System"**
   - Click **"Advanced system settings"**
   - Click **"Environment Variables"**
   - Under **"System variables"**, select **"Path"** and click **"Edit"**
   - Find and remove the entry: `C:\Program Files\Lync`
   - Click **"OK"** on all dialogs

---

## Troubleshooting

### "lync is not recognized as an internal or external command"

**Cause:** PATH has not been updated in your current terminal session.

**Solutions:**
1. Close and reopen your terminal
2. Restart your computer
3. Run `refreshenv` (if using Chocolatey)
4. Manually add to PATH as described above

### "Access Denied" during installation

**Cause:** The installer was not run as Administrator.

**Solution:** Right-click the installer and select "Run as Administrator"

### C compiler not found

**Cause:** No C compiler is available in your PATH.

**Solution:** Install one of the following:
- **GCC/Clang (MinGW):** Download from [winlibs.com](https://winlibs.com)
- **MSVC:** Install Visual Studio with C++ tools
- **Clang (standalone):** Download from [llvm.org](https://llvm.org)

After installing, verify with:
```bash
gcc --version
# or
clang --version
# or
cl
```

### Permission errors when compiling

**Cause:** Trying to write to a protected directory.

**Solution:** Compile in your user directory (e.g., `C:\Users\<username>\Documents\lync-projects\`)

---

## Linux/macOS Installation

For Linux and macOS users, you can install manually:

### Build the compiler

```bash
cmake -B build
cmake --build build
```

### Install to /usr/local/bin

```bash
sudo cp build/lync /usr/local/bin/lync
sudo chmod +x /usr/local/bin/lync
```

### Verify

```bash
lync --help
```

---

## Development Installation

If you're developing the Lync compiler and want to test changes without reinstalling:

1. Build normally:
   ```bash
   cmake --build cmake-build-debug
   ```

2. Run directly from build directory:
   ```bash
   .\cmake-build-debug\Debug\lync.exe myfile.lync
   ```

3. Or create a symbolic link (Windows):
   ```powershell
   New-Item -ItemType SymbolicLink -Path "C:\Program Files\Lync\lync.exe" -Target "C:\Users\bucka\CLionProjects\CMinusMinus\cmake-build-debug\Debug\lync.exe"
   ```

This way, rebuilding automatically updates the installed version.

---

## Next Steps

After installation, check out:
- **[README.md](README.md)** - Language reference and features
- **[CHANGELOG.md](CHANGELOG.md)** - Version history
- **test/** directory - Example Lync programs

Happy coding with Lync! 🚀
