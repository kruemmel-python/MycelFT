# MycelFT Explorer (g++/Konsole ohne Visual Studio)

Diese Anleitung beschreibt, wie der MycelFT Explorer ausschließlich mit der Kommandozeile und **g++** gebaut werden kann, ohne Visual Studio.

## Voraussetzungen

- MSYS2 oder MinGW-w64 (g++)
- Windows SDK (für `comctl32`-Bibliothek und Win32-Header)
  - Bei MSYS2: `mingw-w64-x86_64-toolchain`
- Optional: `cmake` wird **nicht** benötigt

## 1) Build-Ordner anlegen

```bash
cd /path/to/MycelFT/mycelft_explorer
mkdir -p build-gpp
cd build-gpp
```

## 2) Kompilieren mit g++

```bash
g++ -std=c++17 -O2 -municode -DUNICODE -D_UNICODE \
  ../MycelFTExplorer.cpp \
  -o MycelFTExplorer.exe \
  -lcomctl32 -luxtheme -lgdi32
```

### Hinweise
- `-municode` sorgt für einen Unicode-Entry-Point (`wWinMain`).
- `-lcomctl32` ist erforderlich für den ListView-Container.
- `-luxtheme` ist erforderlich für `SetWindowTheme` (Explorer-Style).
- `-lgdi32` ist erforderlich für Schriftarten (CreateFontW/DeleteObject).

## 3) Ausführen

```bash
./MycelFTExplorer.exe
```

## 4) Typische Fehler

**`undefined reference to WinMain`**
- `-municode` fehlt oder falscher Entry-Point.

**`cannot find -lcomctl32`**
- Die Win32-Libraries sind nicht im Linker-Pfad.
- Stelle sicher, dass du die MinGW-w64-Umgebung nutzt.
