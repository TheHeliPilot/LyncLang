================================================================================
                    Lync Compiler v0.2.0 - Installation
================================================================================

QUICK INSTALL (Windows):

1. Right-click "install.bat" and select "Run as Administrator"
   OR
   Right-click PowerShell, select "Run as Administrator", then run:
   .\install.ps1

2. Restart your terminal

3. Test by running:
   lync --help

================================================================================

The installer will:
- Copy lync.exe to C:\Program Files\Lync\
- Add C:\Program Files\Lync\ to your system PATH
- Allow you to run "lync" from any directory

================================================================================

REQUIREMENTS:

- Windows 10 or later
- Administrator privileges
- A C compiler installed (GCC, Clang, or MSVC)

================================================================================

USAGE AFTER INSTALLATION:

  lync myfile.lync          # Compile a program
  lync run myfile.lync      # Compile and run
  lync myfile.lync -o app   # Specify output name
  lync --help               # Show all options

================================================================================

TO UNINSTALL:

Run uninstall.bat or uninstall.ps1 as Administrator

================================================================================

For detailed installation instructions, troubleshooting, and manual installation:
See INSTALL.md

For language documentation and examples:
See README.md

================================================================================
