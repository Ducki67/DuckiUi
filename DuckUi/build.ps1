# ============================================================================
#  DuckUI build script  -  by Ducki67
#
#  Produces, in .\dist :
#     DuckUI_x64_Debug.lib       (static, /MDd /Od /Zi)
#     DuckUI_x64_Release.lib     (static, /MD  /O2)
#     DuckUI.h                   (one monolithic header, auto-links the libs)
#
#  Usage:
#     .\build.ps1                 # fetch ImGui if missing, build both configs
#     .\build.ps1 -Config Release # build one config
#     .\build.ps1 -ImGuiTag v1.91.5
#     .\build.ps1 -Clean
#
#  Requires: Visual Studio 2022 (Build Tools fine) + git. No other deps.
# ============================================================================
[CmdletBinding()]
param(
    [ValidateSet('Both','Debug','Release')]
    [string]$Config   = 'Both',
    [string]$ImGuiTag = 'v1.91.5',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root    = $PSScriptRoot
$imgui   = Join-Path $root 'imgui'
$objRoot = Join-Path $root 'build'
$dist    = Join-Path $root 'dist'

function Info($m) { Write-Host "[DuckUI] $m" -ForegroundColor Cyan }
function Die ($m) { Write-Host "[DuckUI] ERROR: $m" -ForegroundColor Red; exit 1 }

if ($Clean) {
    Info 'Cleaning build/ and dist/ ...'
    Remove-Item -Recurse -Force $objRoot, $dist -ErrorAction SilentlyContinue
    Info 'Done.'
    return
}

# ---------------------------------------------------------------- locate MSVC
function Find-VcvarsAll {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $p = & $vswhere -latest -products * `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath
        if ($p) {
            $v = Join-Path $p 'VC\Auxiliary\Build\vcvarsall.bat'
            if (Test-Path $v) { return $v }
        }
    }
    foreach ($yr in '18','2022') {
        foreach ($base in @("${env:ProgramFiles}\Microsoft Visual Studio\$yr",
                            "${env:ProgramFiles(x86)}\Microsoft Visual Studio\$yr")) {
            foreach ($ed in 'Enterprise','Professional','Community','BuildTools') {
                $v = Join-Path $base "$ed\VC\Auxiliary\Build\vcvarsall.bat"
                if (Test-Path $v) { return $v }
            }
        }
    }
    return $null
}

# Import the x64 MSVC environment from vcvarsall.bat into this PS session once.
function Import-MsvcEnv {
    if ($env:DUCKUI_VCVARS_LOADED) { return }
    $vcvars = Find-VcvarsAll
    if (-not $vcvars) { Die 'vcvarsall.bat not found - install Visual Studio 2022 (with the C++ workload) or Build Tools.' }
    Info "Using MSVC env: $vcvars"
    $tmp = [System.IO.Path]::GetTempFileName()
    # Run vcvars (silencing its stdout+stderr so PS doesn't treat it as fatal),
    # then ALWAYS dump 'set' with '&' - we judge success by cl.exe below, not exit codes.
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    cmd.exe /c "`"$vcvars`" x64 >nul 2>&1 & set > `"$tmp`"" 2>&1 | Out-Null
    $ErrorActionPreference = $old
    Get-Content $tmp | ForEach-Object {
        if ($_ -match '^(.*?)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
    }
    Remove-Item $tmp -ErrorAction SilentlyContinue
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { Die 'cl.exe still not on PATH after loading vcvars.' }
    $env:DUCKUI_VCVARS_LOADED = '1'
}

# ------------------------------------------------------------- fetch Dear ImGui
function Ensure-ImGui {
    if (Test-Path (Join-Path $imgui 'imgui.cpp')) {
        Info "Dear ImGui present at .\imgui"
        return
    }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Die "Dear ImGui not found at .\imgui and git is unavailable. Either install git or unzip Dear ImGui into .\imgui manually."
    }
    Info "Cloning Dear ImGui $ImGuiTag ..."
    # git writes progress to stderr; don't let that trip ErrorActionPreference=Stop.
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & git clone --depth 1 --branch $ImGuiTag 'https://github.com/ocornut/imgui.git' $imgui 2>&1 | ForEach-Object { Write-Host $_ }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    if ($code -ne 0) { Die "git clone failed (tag '$ImGuiTag'?)." }
}

# --------------------------------------------------------------- the 6 sources
$sources = @(
    'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp',
    'imgui_demo.cpp',   # bundled so ImGui::ShowDemoWindow() links out of the box
    'backends\imgui_impl_win32.cpp', 'backends\imgui_impl_dx11.cpp'
)

function Build-Config([string]$cfg) {
    $isDebug = ($cfg -eq 'Debug')
    $obj     = Join-Path $objRoot $cfg
    New-Item -ItemType Directory -Force $obj, $dist | Out-Null

    $inc = @("/I`"$imgui`"", "/I`"$imgui\backends`"")
    $common = @('/nologo','/c','/std:c++17','/EHsc','/W3','/DUNICODE','/D_UNICODE',
                '/DWIN32_LEAN_AND_MEAN','/DIMGUI_DEFINE_MATH_OPERATORS')
    # NB: no /GL (whole-program opt). It would force every consumer to add /LTCG
    # and emit a link warning otherwise - the opposite of "zero config". /O2 /Oi
    # already gives full per-TU optimization for a drop-in static lib.
    if ($isDebug) { $flags = $common + @('/MDd','/Od','/Zi','/D_DEBUG',
                                         "/Fd`"$obj\DuckUI.pdb`"") }
    else          { $flags = $common + @('/MD','/O2','/Oi','/DNDEBUG') }

    Info "Compiling $cfg ..."
    $objs = @()
    foreach ($s in $sources) {
        $srcPath = Join-Path $imgui $s
        if (-not (Test-Path $srcPath)) { Die "missing source: $srcPath" }
        $o = Join-Path $obj ([IO.Path]::GetFileNameWithoutExtension($s) + '.obj')
        $objs += $o
        & cl.exe @flags @inc "/Fo$o" "$srcPath" | Out-Host
        if ($LASTEXITCODE -ne 0) { Die "compile failed: $s" }
    }

    $libName = "DuckUI_x64_$cfg.lib"
    $libOut  = Join-Path $dist $libName
    Info "Archiving $libName ..."
    $libFlags = @('/nologo','/MACHINE:X64')
    & lib.exe @libFlags "/OUT:$libOut" @objs | Out-Host
    if ($LASTEXITCODE -ne 0) { Die "lib.exe failed for $cfg" }
    Info "  -> $libOut"
}

# ------------------------------------------------ assemble monolithic DuckUI.h
function Build-Header {
    Info 'Assembling monolithic DuckUI.h ...'
    $prefix  = Get-Content (Join-Path $root 'src\duckui_prefix.h')  -Raw
    $wrapper = Get-Content (Join-Path $root 'src\duckui_wrapper.h') -Raw

    # Resolve ImGui version string from imgui.h for the banner.
    $ver = 'unknown'
    $h = Get-Content (Join-Path $imgui 'imgui.h')
    foreach ($l in $h) { if ($l -match '#define\s+IMGUI_VERSION\s+"([^"]+)"') { $ver = $matches[1]; break } }

    $debugLib   = (Join-Path $dist 'DuckUI_x64_Debug.lib')
    $releaseLib = (Join-Path $dist 'DuckUI_x64_Release.lib')
    $buildInfo  = "ImGui $ver, $ImGuiTag, built $(Get-Date -Format 'yyyy-MM-dd HH:mm')"

    # Embed a vendor header verbatim, but strip:
    #   - its own '#pragma once' (we have one at the top of DuckUI.h), and
    #   - any local-quote '#include "<sibling>"' for headers we inline below
    #     (otherwise the compiler would pull the real file from disk - which the
    #     end-user does not have - and double-define everything).
    $siblings = 'imconfig\.h|imgui\.h|imgui_user\.h|imgui_internal\.h'
    function Embed([string]$relPath, [string]$banner) {
        $full = Join-Path $imgui $relPath
        if (-not (Test-Path $full)) { Die "missing header: $full" }
        $lines = Get-Content $full
        $sb = [System.Text.StringBuilder]::new()
        [void]$sb.AppendLine("// ===== BEGIN $banner =====")
        foreach ($l in $lines) {
            if ($l -match '^\s*#pragma\s+once\s*$') { continue }
            if ($l -match "^\s*#include\s+`"($siblings)`"") {
                [void]$sb.AppendLine("// [DuckUI] inlined: $($l.Trim())")
                continue
            }
            [void]$sb.AppendLine($l)
        }
        [void]$sb.AppendLine("// ===== END $banner =====")
        return $sb.ToString()
    }

    # Quote a path as a C string literal for #pragma comment(lib, ...).
    function QuoteForC([string]$p) { return '"' + ($p -replace '\\','\\') + '"' }

    $prefix = $prefix.Replace('@IMGUI_VERSION@', $ver)
    $prefix = $prefix.Replace('@DUCKUI_BUILD_INFO@', $buildInfo)
    $prefix = $prefix.Replace('@DUCKUI_LIB_DEBUG@',   (QuoteForC $debugLib))
    $prefix = $prefix.Replace('@DUCKUI_LIB_RELEASE@', (QuoteForC $releaseLib))

    $out = [System.Text.StringBuilder]::new()
    [void]$out.Append($prefix)
    [void]$out.AppendLine()
    [void]$out.Append((Embed 'imconfig.h'                'imconfig.h'))
    [void]$out.AppendLine()
    [void]$out.Append((Embed 'imgui.h'                   'imgui.h'))
    [void]$out.AppendLine()
    [void]$out.Append((Embed 'imgui_internal.h'          'imgui_internal.h'))
    [void]$out.AppendLine()
    [void]$out.Append((Embed 'backends\imgui_impl_win32.h' 'imgui_impl_win32.h'))
    [void]$out.AppendLine()
    [void]$out.Append((Embed 'backends\imgui_impl_dx11.h'  'imgui_impl_dx11.h'))
    [void]$out.AppendLine()
    [void]$out.Append($wrapper)

    $outPath = Join-Path $dist 'DuckUI.h'
    # UTF-8 (no BOM) so the leading bytes don't upset the compiler.
    [System.IO.File]::WriteAllText($outPath, $out.ToString(), (New-Object System.Text.UTF8Encoding($false)))
    Info "  -> $outPath  (bundles ImGui $ver)"
}

# --------------------------------------------------------------------- run it
$swatch = [System.Diagnostics.Stopwatch]::StartNew()
Ensure-ImGui
Import-MsvcEnv
New-Item -ItemType Directory -Force $dist | Out-Null

switch ($Config) {
    'Both'    { Build-Config 'Debug'; Build-Config 'Release' }
    default   { Build-Config $Config }
}

# The header bakes in absolute paths to BOTH libs, so always (re)write it.
Build-Header

# Drop ImGui's license next to the dist for compliance.
$lic = Join-Path $imgui 'LICENSE.txt'
if (Test-Path $lic) { Copy-Item $lic (Join-Path $dist 'LICENSE-imgui.txt') -Force }

$swatch.Stop()
Info ("Build complete in {0:N1}s. Ship dist\DuckUI.h + the .lib(s)." -f $swatch.Elapsed.TotalSeconds)
pause
