# MycelFT Build & Installation Guide

Diese Anleitung beschreibt, wie der **MycelFT Explorer** als Windows-GUI-Tool gebaut wird und wie er zusammen mit dem Dateisystem-Code (FastFat-Tree) genutzt werden kann.

## Voraussetzungen

### Windows (Explorer)
- Windows 10/11
- Visual Studio 2022 (oder Build Tools) mit:
  - **Desktop development with C++**
  - Windows 10/11 SDK
- CMake (>= 3.16)

### Kernel / Dateisystem (FastFat)
- Windows Driver Kit (WDK) passend zur verwendeten Windows-Version
- Visual Studio + WDK Integration
- Kernel-Testsignatur/Testszenario (je nach Umgebung)

> **Hinweis:** Der Explorer ist ein User-Mode-Tool und benötigt keine WDK-Installation. Der FastFat-Tree bleibt separat, wird aber im selben Repo verwaltet.

---

## 1) MycelFT Explorer kompilieren (Windows GUI)

Der Explorer liegt unter `mycelft_explorer/` und ist eine eigenständige Win32-GUI-Anwendung.

### Schritte (CMake + Visual Studio)

1. **Build-Ordner erstellen**
   ```powershell
   cd /path/to/MycelFT
   mkdir build-explorer
   cd build-explorer
   ```

2. **CMake konfigurieren**
   ```powershell
   cmake -S ../mycelft_explorer -B . -G "Visual Studio 17 2022" -A x64
   ```

3. **Kompilieren**
   ```powershell
   cmake --build . --config Release
   ```

4. **Binary finden**
   ```text
    build-explorer/Release/MycelFTExplorer.exe
    ```

### Alternative: g++ (Konsole)

Für eine reine Konsolen-Build-Variante (MinGW/MSYS2) siehe auch
`mycelft_explorer/compile.md`. Kurzfassung:

```bash
g++ -std=c++17 -O2 -municode -DUNICODE -D_UNICODE \
  mycelft_explorer/MycelFTExplorer.cpp \
  -o MycelFTExplorer.exe \
  -lcomctl32 -luxtheme -lgdi32 -lbcrypt -lshell32
```

### Funktion des Explorers
- Zeigt Dateien eines Ordners an (Standard: `C:\`).
- Eintrag auswählen → **Encrypt/Decrypt** klicken.
  - Der Algorithmus ist symmetrisch (XOR-Key), daher gilt:
    - **Einmal klicken** = verschlüsseln
    - **Nochmals klicken** = wieder entschlüsseln
- Der Key wird deterministisch aus Dateiname + Volume-Serial erzeugt, wie im Kernel-Code.

---

## 2) FastFat / MycelFT Filesystem bauen

Der Kernel-Code liegt unter `fastfat/`. Um ihn zu kompilieren:

### Schritte (Visual Studio + WDK)
1. **WDK installieren** (Version passend zu VS).
2. In Visual Studio:
   - Lösung/Projekt für FastFat öffnen (falls vorhanden) oder mit WDK-Templates einbinden.
3. Build-Konfiguration:
   - **x64** + **Release** oder **Debug**
4. Kompilieren:
   - Visual Studio Build (oder msbuild)

> **Wichtig:** Der Treiber benötigt Testsignatur (Test Mode), wenn er nicht signiert ist.

---

## 3) Zusammenspiel Explorer ↔ Filesystem

Der Explorer nutzt denselben deterministischen Seed-Mechanismus wie der Kernel:

1. **Dateiname + Volume-Serial** → Seed
2. Seed erzeugt ein simuliertes Mycel-Gitter (16×16)
3. Das Gitter generiert einen Key-Stream
4. XOR auf Datei-Daten

Damit kann der Explorer die gleichen Dateien ver-/entschlüsseln wie das Filesystem:

✅ Wenn der Treiber aktiv ist, wird beim Lesen/Schreiben automatisch entschlüsselt.  
✅ Der Explorer kann die Datei manuell transformieren (z.B. für Offline-Test).

---

## 4) Workflow-Beispiel

1. Treiber bauen und laden (FastFat/MycelFT).
2. Datei per Explorer verarbeiten (Encrypt).
3. Datei im System öffnen → wird im Treiber wieder entschlüsselt.
4. Optional: Nochmals im Explorer → Datei wieder im Klartext.

---

## 5) Troubleshooting

**Explorer zeigt keine Dateien an**
- Pfad im oberen Feld prüfen (z.B. `C:\Users\`).
- Rechte prüfen (z.B. Administrator).

**Datei nach Transformation unlesbar**
- Dateiname darf nicht verändert werden, sonst ändert sich der Seed.
- Prüfen, ob dieselbe Volume-Serial verwendet wird.

**Build schlägt fehl**
- CMake-Version prüfen (`cmake --version`).
- Visual Studio/SDK Installation kontrollieren.
