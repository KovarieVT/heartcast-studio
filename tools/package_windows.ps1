<#
  Assembles the distributable KovaLive! folder (OBS-compatible layout):

    dist\KovaLive\
      bin\64bit\KovaLive.exe   + Qt runtime (windeployqt) + libobs runtime DLLs
      obs-plugins\64bit\*.dll          engine modules only (no OBS UI plugins)
      data\libobs\*                    shaders/effects
      data\obs-plugins\<module>\*      module data (incl. win-dshow virtual camera DLLs)
      data\kovalive\{overlays,scenes} app data
      docs\*.md / *.html               documentation
      redist\vc_redist.x64.exe         (optional) MSVC runtime for the installer

  Usage (from repo root, after building obs-studio and KovaLive):
    pwsh tools\package_windows.ps1 -ObsInstall C:\obs-dist -AppExe build\bin\Release\KovaLive.exe -QtBin $env:QT_ROOT_DIR\bin
#>
param(
  [Parameter(Mandatory)] [string] $ObsInstall,
  [Parameter(Mandatory)] [string] $AppExe,
  [Parameter(Mandatory)] [string] $QtBin,
  [string] $Out = "dist\KovaLive",
  [string] $AppStage = "",      # `cmake --install` prefix of the app build (obs-vst3 module + scanner)
  [string] $VcRedist = ""
)
$ErrorActionPreference = "Stop"

# Engine modules KovaLive loads (must match the allowlist in src/engine/Engine.cpp)
$Modules = @("image-source","obs-ffmpeg","obs-filters","obs-outputs","obs-transitions","obs-x264",
             "rtmp-services","obs-text","obs-vst","obs-webrtc","obs-nvenc","nv-filters","obs-qsv11",
             "win-capture","win-dshow","win-wasapi","coreaudio-encoder","decklink","aja","vlc-video","obs-libfdk")

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
$bin = New-Item -ItemType Directory -Force "$Out\bin\64bit"
New-Item -ItemType Directory -Force "$Out\obs-plugins\64bit","$Out\data","$Out\docs","$Out\redist" | Out-Null

# 1) libobs runtime: everything in OBS bin\64bit except the OBS app itself, its UI and its Qt copy
Get-ChildItem "$ObsInstall\bin\64bit" -File | Where-Object {
    $_.Name -notmatch '^(obs64\.exe|obs-frontend-api\.dll|Qt6.*\.dll)$'
} | Copy-Item -Destination $bin
# obs-ffmpeg-mux.exe is REQUIRED for recording / replay buffer
if (-not (Test-Path "$bin\obs-ffmpeg-mux.exe")) { throw "obs-ffmpeg-mux.exe missing from OBS install" }

# 2) engine plugins + their data
foreach ($m in $Modules) {
  $dll = "$ObsInstall\obs-plugins\64bit\$m.dll"
  if (Test-Path $dll) { Copy-Item $dll "$Out\obs-plugins\64bit\" }
  $pdb = "$ObsInstall\obs-plugins\64bit\$m.pdb"
  if (Test-Path "$ObsInstall\data\obs-plugins\$m") {
    Copy-Item -Recurse "$ObsInstall\data\obs-plugins\$m" "$Out\data\obs-plugins\$m"
  }
}
# win-capture helpers (game capture hook/injector) live next to the plugin data
Copy-Item -Recurse "$ObsInstall\data\libobs" "$Out\data\libobs"

# 2b) VST3 host built with the app (third_party/obs-vst3, GPL-3.0-or-later)
if ($AppStage) {
  $vst3 = "$AppStage\obs-plugins\64bit\obs-vst3.dll"
  if (-not (Test-Path $vst3)) { throw "obs-vst3.dll missing from $AppStage — was the app built with -DKOVALIVE_BUILD_VST3=ON?" }
  Copy-Item $vst3 "$Out\obs-plugins\64bit\"
  if (Test-Path "$AppStage\obs-plugins\64bit\obs-vst3.pdb") { Copy-Item "$AppStage\obs-plugins\64bit\obs-vst3.pdb" "$Out\obs-plugins\64bit\" }
  Copy-Item "$AppStage\bin\64bit\obs-vst3-scanner.exe" $bin
  if (Test-Path "$AppStage\data\obs-plugins\obs-vst3") { Copy-Item -Recurse "$AppStage\data\obs-plugins\obs-vst3" "$Out\data\obs-plugins\obs-vst3" }
}

# 3) our executable + Qt runtime
Copy-Item $AppExe $bin
& "$QtBin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$bin\KovaLive.exe"
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
# obs-vst (VST2) and obs-vst3 use Qt Widgets from the same folder; make sure nothing they need is missing
foreach ($p in @("$Out\obs-plugins\64bit\obs-vst.dll", "$Out\obs-plugins\64bit\obs-vst3.dll")) {
  if (Test-Path $p) {
    & "$QtBin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --dir $bin $p
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed for $p" }
  }
}

# 4) app data + docs (+ HTML render of the user guide for the installer's finish page)
New-Item -ItemType Directory -Force "$Out\data\kovalive" | Out-Null
Copy-Item -Recurse resources\overlays "$Out\data\kovalive\overlays"
Copy-Item -Recurse resources\scenes   "$Out\data\kovalive\scenes"
Copy-Item -Recurse resources\stingers "$Out\data\kovalive\stingers"
Copy-Item docs\*.md "$Out\docs\"
Copy-Item docs\THIRD-PARTY-LICENSES.md "$Out\THIRD-PARTY-LICENSES.md"
$guide = Get-Content docs\USER-GUIDE.md -Raw
$html = "<html><head><meta charset='utf-8'><title>KovaLive! — Getting Started</title>" +
        "<style>body{font-family:Segoe UI,sans-serif;max-width:860px;margin:40px auto;color:#E6DED2;background:#0B0A0D}h1,h2,h3{color:#B8935A}a{color:#E0606F}code{background:#1D1922;color:#F3ECDF;padding:2px 5px;border-radius:2px}</style></head><body><pre style='white-space:pre-wrap;font-family:inherit'>" +
        [System.Net.WebUtility]::HtmlEncode($guide) + "</pre></body></html>"
Set-Content -Encoding UTF8 "$Out\docs\USER-GUIDE.html" $html

# 5) VC++ runtime for the installer
if ($VcRedist -and (Test-Path $VcRedist)) { Copy-Item $VcRedist "$Out\redist\vc_redist.x64.exe" }

Write-Host "Packaged to $Out"
Get-ChildItem -Recurse $Out | Measure-Object -Property Length -Sum | ForEach-Object { "{0:N1} MB in {1} files" -f ($_.Sum/1MB), $_.Count }
