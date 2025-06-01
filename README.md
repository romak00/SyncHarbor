
# SyncHarbor

Cross‑platform command‑line utility that keeps a local folder in sync with several cloud
providers (Google Drive, Dropbox, …).  
Works on **Linux, macOS and Windows** and runs as a background daemon.

---

## 1. Building the project

### 1.1  Linux / macOS

```bash
# 1. fetch sources
git clone git@github.com:romak00/SyncHarbor.git
cd syncharbor

# 2. install build tool‑chain & third‑party libraries
./scripts/bootstrap.sh            # will sudo‑install the required packages

# 3. configure & compile (Debug | RelWithDebInfo | Release)
./scripts/build.sh Release

# 4. optional – install as a binary to /usr/local
cd build
sudo make install
```

### 1.2  Windows

```powershell
git clone git@github.com:romak00/SyncHarbor.git
cd syncharbor

# Either:
#   – open the top‑level CMakeLists.txt in Visual Studio 2022 and build
# or:
#   – use the helper scripts (requires Git‑Bash or PowerShell)

powershell -File scripts\bootstrap.ps1      # installs curl & sqlite via vcpkg
powershell -File scripts\build.ps1 Release  # builds with Ninja / MSVC
```

All binaries are placed in `build/bin/`.  
The main executable is **`syncharbor`** (or `syncharbor.exe` on Windows).

---

## 2. Running SyncHarbor

### 2.1  First‑time initialisation

```bash
syncharbor --config <profile_name>
```

* select / create a *profile* (configuration name)  
* pick a local folder to sync  
* add one or more cloud accounts (the browser will open for OAuth)  

An **initial synchronisation** is performed and a SQLite database is created
in:

```text
$XDG_DATA_HOME/syncharbor/<profile_name>/<profile_name>.sqlite3
```
(on Windows it lives under `%APPDATA%\SyncHarbor\…`).

### 2.2 Logging

* **Debug** and **RelWithDebInfo** builds are compiled with
  `ENABLE_LOGGING=ON`, so the application produces verbose diagnostics.

  | Phase              | Log file                                         |
  |--------------------|--------------------------------------------------|
  | Initial sync       | `<data-dir>/<config>/logs/syncharbor-initial.log` |
  | Daemon operation   | `<data-dir>/<config>/logs/syncharbor-daemon.log`  |

  These files are rotated automatically when you restart the tool.

### 2.3  Starting & stopping the background service

```bash
# start in the background
syncharbor --run-daemon <profile_name>

# ask the daemon to terminate gracefully
syncharbor --stop-daemon <profile_name>
```

### 2.4  Other handy commands

```bash
syncharbor --show-data-dir <profile_name>   # print the profile's data directory
```

---

## 3. Running the unit / integration tests

```bash
./scripts/tests.sh Debug
```

The helper script is simply a convenience wrapper around `ctest` that groups
tests by fixture.

