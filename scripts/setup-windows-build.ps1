# DFHack + df-ai Windows Build Environment Setup
# Run from an elevated PowerShell prompt (right-click -> Run as Administrator)
#
# Prerequisites you already have:
#   - Visual Studio 2022 (with "Desktop development with C++" workload)
#   - Git for Windows
#   - df-ai cloned at "G:\Useful Programs\df-ai"
#
# What this script does:
#   1. Downloads and installs Python 3.12 to G:\Useful Programs\Python3
#   2. Installs Jinja2 via pip
#   3. Clones DFHack (with submodules) to G:\Useful Programs\dfhack
#   4. Links df-ai as an external DFHack plugin
#   5. Runs CMake configure + build via VS 2022

param(
    [string]$BaseDir = "G:\Useful Programs",
    [string]$DfInstallDir = "",
    [switch]$BuildOnly,
    [switch]$Install
)

$ErrorActionPreference = "Stop"

$PythonDir  = Join-Path $BaseDir "Python3"
$DfhackDir  = Join-Path $BaseDir "dfhack"
$DfAiDir    = Join-Path $BaseDir "df-ai"
$BuildDir   = Join-Path $DfhackDir "build"
$PythonExe  = Join-Path $PythonDir "python.exe"
$PipExe     = Join-Path $PythonDir "Scripts\pip.exe"

# ---------- helpers ----------

function Write-Step($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "  $msg" -ForegroundColor Green }
function Write-Skip($msg) { Write-Host "  $msg" -ForegroundColor Yellow }

function Find-VsCmake {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path $vswhere)) {
        throw "vswhere.exe not found. Is Visual Studio 2022 installed?"
    }
    $vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
    if (!$vsPath) {
        throw "No VS 2022 installation with C++ workload found."
    }
    $cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (!(Test-Path $cmake)) {
        throw "CMake not found at $cmake. Make sure 'Desktop development with C++' workload is installed."
    }
    return $cmake
}

# ---------- skip ahead if re-running just to build ----------

if ($BuildOnly) {
    Write-Step "Rebuild (skipping setup)"
    if (!(Test-Path "$BuildDir\CMakeCache.txt")) {
        throw "No build directory found at $BuildDir. Run without -BuildOnly first to do full setup."
    }
    $env:PATH = "$PythonDir;$PythonDir\Scripts;$env:PATH"
    $cmake = Find-VsCmake
    & $cmake --build "$BuildDir" --config Release
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
    Write-Ok "Build succeeded. Plugin DLL at: $BuildDir\plugins\Release\df-ai.dll"
    exit 0
}

# ---------- 1. Python (embeddable package — no installer needed) ----------

Write-Step "Step 1/5: Python 3.12"

if (Test-Path $PythonExe) {
    Write-Skip "Already installed at $PythonDir"
} else {
    $zipUrl = "https://www.python.org/ftp/python/3.12.4/python-3.12.4-embed-amd64.zip"
    $zipPath = Join-Path $env:TEMP "python-3.12.4-embed-amd64.zip"

    Write-Host "  Downloading Python 3.12.4 embeddable package..."
    Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath -UseBasicParsing

    Write-Host "  Extracting to $PythonDir..."
    New-Item -ItemType Directory -Path $PythonDir -Force | Out-Null
    Expand-Archive -Path $zipPath -DestinationPath $PythonDir -Force
    Remove-Item $zipPath -ErrorAction SilentlyContinue

    # Enable pip: uncomment "import site" in the ._pth file
    $pthFile = Get-ChildItem $PythonDir -Filter "python*._pth" | Select-Object -First 1
    if ($pthFile) {
        $content = Get-Content $pthFile.FullName
        $content = $content -replace "^#import site", "import site"
        Set-Content $pthFile.FullName $content
        Write-Ok "Enabled site-packages in $($pthFile.Name)"
    }

    # Bootstrap pip
    $getPipUrl = "https://bootstrap.pypa.io/get-pip.py"
    $getPipPath = Join-Path $env:TEMP "get-pip.py"
    Write-Host "  Installing pip..."
    Invoke-WebRequest -Uri $getPipUrl -OutFile $getPipPath -UseBasicParsing
    & "$PythonExe" $getPipPath --no-warn-script-location
    Remove-Item $getPipPath -ErrorAction SilentlyContinue

    if (!(Test-Path $PythonExe)) {
        throw "Python setup failed. $PythonExe not found."
    }
    Write-Ok "Python installed."
}

$env:PATH = "$PythonDir;$PythonDir\Scripts;$env:PATH"

# ---------- 2. Jinja2 ----------

Write-Step "Step 2/5: Jinja2"

$jinja2Check = & $PythonExe -c "import jinja2; print('ok')" 2>&1
if ($jinja2Check -eq "ok") {
    Write-Skip "Jinja2 already installed."
} else {
    & $PythonExe -m pip install --no-warn-script-location Jinja2
    Write-Ok "Jinja2 installed."
}

# ---------- 3. DFHack ----------

Write-Step "Step 3/5: Clone DFHack"

if (Test-Path (Join-Path $DfhackDir ".git")) {
    Write-Skip "Already cloned at $DfhackDir"
    Write-Host "  Updating..."
    Push-Location $DfhackDir
    git pull --ff-only
    git submodule update --init --recursive
    Pop-Location
} else {
    Write-Host "  Cloning DFHack (this will take several minutes)..."
    git clone --recursive https://github.com/DFHack/dfhack.git "$DfhackDir"
    Push-Location $DfhackDir
    git checkout develop
    git submodule update --init --recursive
    Pop-Location
    Write-Ok "DFHack cloned."
}

# ---------- 4. Link df-ai ----------

Write-Step "Step 4/5: Link df-ai as external plugin"

if (!(Test-Path $DfAiDir)) {
    throw "df-ai not found at $DfAiDir. Clone it there first."
}

$externalDir = Join-Path $DfhackDir "plugins\external"
$linkPath    = Join-Path $externalDir "df-ai"

if (!(Test-Path $externalDir)) {
    New-Item -ItemType Directory -Path $externalDir -Force | Out-Null
}

if (Test-Path $linkPath) {
    Write-Skip "Already linked at $linkPath"
} else {
    New-Item -ItemType Junction -Path $linkPath -Target $DfAiDir | Out-Null
    Write-Ok "Created junction: $linkPath -> $DfAiDir"
}

# ---------- 5. CMake configure + build ----------

Write-Step "Step 5/5: Configure and build"

$cmake = Find-VsCmake
Write-Host "  Using CMake: $cmake"

if (!(Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Host "  Running CMake configure..."
    $configArgs = @(
        "-S", "$DfhackDir",
        "-B", "$BuildDir",
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DPython3_ROOT_DIR=$PythonDir"
    )
    if ($DfInstallDir) {
        $configArgs += "-DCMAKE_INSTALL_PREFIX=$DfInstallDir"
    }
    & $cmake @configArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }
    Write-Ok "Configure succeeded."
} else {
    Write-Skip "Already configured (delete $BuildDir\CMakeCache.txt to reconfigure)"
}

Write-Host "  Building (this will take a while on first run)..."
& $cmake --build "$BuildDir" --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

# ---------- done ----------

$pluginDll = Join-Path $BuildDir "plugins\Release\df-ai.dll"
Write-Host ""
Write-Host "==========================================" -ForegroundColor Green
Write-Host "  Build complete!" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green
Write-Host ""

if (Test-Path $pluginDll) {
    Write-Host "  df-ai.dll: $pluginDll"
} else {
    Write-Host "  DFHack built. df-ai.dll may have failed separately -- check build output above." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "  To install into Dwarf Fortress, re-run with:" -ForegroundColor White
Write-Host "    .\setup-windows-build.ps1 -Install -DfInstallDir 'C:\path\to\Dwarf Fortress'" -ForegroundColor White
Write-Host ""
Write-Host "  To rebuild after code changes:" -ForegroundColor White
Write-Host "    .\setup-windows-build.ps1 -BuildOnly" -ForegroundColor White
Write-Host ""
Write-Host "  Then in DFHack console: enable df-ai" -ForegroundColor White

# ---------- optional install ----------

if ($Install -and $DfInstallDir) {
    Write-Step "Installing to $DfInstallDir"
    & $cmake --build "$BuildDir" --config Release --target INSTALL
    if ($LASTEXITCODE -ne 0) { throw "Install failed." }
    Write-Ok "Installed. Launch DF from Steam, then run: enable df-ai"
}
