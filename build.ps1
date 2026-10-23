param(
    [ValidateSet('x64', 'Win32')][string]$Platform = 'x64',
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$sdkVersion = '1.0.4258.31'
$sdkDirectory = Join-Path $PSScriptRoot "packages\Microsoft.Web.WebView2.$sdkVersion"
if (-not (Test-Path -LiteralPath (Join-Path $sdkDirectory 'build\native\include\WebView2.h'))) {
    New-Item -ItemType Directory -Path $sdkDirectory -Force | Out-Null
    $archivePath = Join-Path $PSScriptRoot 'packages\webview2-sdk.zip'
    Invoke-WebRequest -Uri "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$sdkVersion/microsoft.web.webview2.$sdkVersion.nupkg" -OutFile $archivePath
    Expand-Archive -LiteralPath $archivePath -DestinationPath $sdkDirectory -Force
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ build tools were not found.' }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild was not found.' }
& $msbuild (Join-Path $PSScriptRoot 'Liteview.vcxproj') /nologo /m /verbosity:minimal "/p:Configuration=$Configuration" "/p:Platform=$Platform"
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
$outputPath = if ($Configuration -eq 'Debug') { "build\$Platform\Debug\bin\Liteview.exe" } else { "dist\$Platform\Liteview.exe" }
$exe = Get-Item -LiteralPath (Join-Path $PSScriptRoot $outputPath)
Write-Host ("Built {0} ({1:N1} KiB, {2:N0} bytes)" -f $exe.FullName, ($exe.Length / 1KB), $exe.Length)
if ($Configuration -eq 'Release') {
    $packageArch = if ($Platform -eq 'Win32') { 'x86' } else { 'x64' }
    $portableZip = Join-Path $PSScriptRoot "dist\Liteview-portable-$packageArch.zip"
    # Explicit allowlist: never package browsing profiles, SDKs, or build intermediates.
    $packageFiles = @($exe.FullName, (Join-Path $exe.DirectoryName 'README.txt'), (Join-Path $exe.DirectoryName 'WebView2-LICENSE.txt'))
    Compress-Archive -LiteralPath $packageFiles -DestinationPath $portableZip -Force
    Write-Host "Portable package: $portableZip"
}
