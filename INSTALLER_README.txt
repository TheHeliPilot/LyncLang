================================================================================
                    Lync Compiler v0.2.1 - Installation
================================================================================

QUICK INSTALL (Windows):

1. Copy lync.exe to the same folder as install.bat

2. Right-click "install.bat" and select "Run as Administrator"

3. Restart your terminal

4. Test by running:
   lync --help

================================================================================

The installer will:
- Copy lync.exe to C:\Program Files\Lync\
- Add C:\Program Files\Lync\ to your system PATH (if not already added)
- Allow you to run "lync" from any directory

================================================================================

REQUIREMENTS:

- Windows 10 or later
- Administrator privileges
- A C compiler installed (GCC, Clang, or MSVC)
- lync.exe in the same directory as install.bat

================================================================================

USAGE AFTER INSTALLATION:

  lync myfile.lync          # Compile a program
  lync run myfile.lync      # Compile and run
  lync myfile.lync -o app   # Specify output name
  lync --help               # Show all options

================================================================================

TO UNINSTALL:

Run uninstall.bat as Administrator

================================================================================

For detailed installation instructions, troubleshooting, and manual installation:
See INSTALL.md

For language documentation and examples:
See README.md

================================================================================
