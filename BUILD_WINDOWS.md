# Build on Windows (Cursor / VS Code)

You **do not need MinGW** for this project. Use **Visual Studio 2022** (C++20) + **CMake**.

## One-time setup

### 1. Restart Cursor

After installing CMake (`winget install Kitware.CMake`), **fully quit and reopen Cursor** so `cmake` is on PATH.

### 2. Open the correct folder

**File → Open Folder** → select:

`D:\hft2\Limit-Order-Book`

(not the parent `hft2` folder).

### 3. Install extensions (Cursor will prompt)

- **C/C++** (`ms-vscode.cpptools`)
- **CMake Tools** (`ms-vscode.cmake-tools`)

### 4. Fix compiler PATH (important)

Old MinGW (`C:\MinGW\bin\g++` GCC 6.3) does **not** support C++20.

**Option A (recommended):** Move MinGW down in system PATH

1. Windows Search → **Environment Variables**
2. Edit **Path** under your user account
3. Move `C:\MinGW\bin` **below** Visual Studio entries, or remove it if unused
4. Restart Cursor

**Option B:** Only use the project build script (ignores MinGW for builds):

```powershell
.\build.ps1
.\build.ps1 -Test
```

### 5. Verify in Cursor terminal

Open terminal (**Ctrl+`**) and run:

```powershell
cmake --version
# should show 4.x

.\build.ps1 -Test
# should print: gateway tests passed
```

## Build from Cursor

| Action | How |
|--------|-----|
| Default build | **Ctrl+Shift+B** (task: Build Release) |
| Gateway test | Terminal: `.\build.ps1 -Test` |
| 5M order benchmark | Terminal: `.\build.ps1 -Run` |
| CMake GUI build | CMake Tools: **Build** button (configure kit: VS 2022 amd64) |

Executables: `build\Release\LimitOrderBook.exe`, `build\Release\lob_gateway_test.exe`

## CMake Tools kit selection

1. **Ctrl+Shift+P** → `CMake: Select a Kit`
2. Choose **Visual Studio Community 2022 Release - amd64** (or similar)
3. **Ctrl+Shift+P** → `CMake: Configure`
4. **Ctrl+Shift+P** → `CMake: Build`

## If `cmake` is not found

Add to user PATH manually:

`C:\Program Files\CMake\bin`

Or in Cursor settings, this repo already prepends that path for the integrated terminal (see `.vscode/settings.json`).

## C++20 without Visual Studio?

If you cannot use VS 2022, install a modern compiler:

```powershell
winget install BrechtSanders.WinLibs.POSIX.UCRT
```

Then add its `bin` folder to PATH (GCC 14+, C++20). Prefer VS 2022 for this repo.
