# Builds bin\Slice75HE.exe with the MSVC toolchain (VS 2022 Build Tools).
param([switch]$Debug)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC (VS 2022 Build Tools, C++ workload) not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$obj = Join-Path $root 'build\obj'
$bin = Join-Path $root 'bin'
New-Item -ItemType Directory -Force $obj, $bin | Out-Null

$imgui = Join-Path $root 'third_party\imgui'
if (-not (Test-Path "$imgui\imgui.cpp")) {
    # Dear ImGui is fetched on first build (pinned release) instead of being vendored in the repo.
    $tp = Join-Path $root 'third_party'
    New-Item -ItemType Directory -Force $tp | Out-Null
    $zip = Join-Path $tp 'imgui.zip'
    Invoke-WebRequest 'https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.zip' -OutFile $zip -UseBasicParsing
    Expand-Archive $zip $tp -Force
    Remove-Item $zip
    Move-Item (Join-Path $tp 'imgui-1.91.9') $imgui
}
$sources = @(Get-ChildItem (Join-Path $root 'src') -Filter *.cpp | ForEach-Object FullName) + @(
    "$imgui\imgui.cpp", "$imgui\imgui_draw.cpp", "$imgui\imgui_tables.cpp", "$imgui\imgui_widgets.cpp",
    "$imgui\backends\imgui_impl_win32.cpp", "$imgui\backends\imgui_impl_dx11.cpp")

$opt = if ($Debug) { '/Od /Zi /MTd' } else { '/O2 /MT /DNDEBUG' }
$cflags = "/nologo /std:c++20 /EHsc /W3 /utf-8 $opt /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX " +
          "/I`"$imgui`" /I`"$imgui\backends`" /Fo`"$obj\\`""
$libs = 'd3d11.lib d3dcompiler.lib dxgi.lib hid.lib setupapi.lib user32.lib gdi32.lib shell32.lib ole32.lib dwmapi.lib comdlg32.lib'

$rc = Join-Path $root 'src\app.rc'
$res = Join-Path $obj 'app.res'
$src = ($sources | ForEach-Object { "`"$_`"" }) -join ' '
$cmd = "`"$vcvars`" >nul && rc /nologo /fo `"$res`" `"$rc`" && " +
       "cl $cflags $src `"$res`" /Fe`"$bin\Slice75HE.exe`" /link /SUBSYSTEM:WINDOWS $libs"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
Write-Host "OK: $bin\Slice75HE.exe"
