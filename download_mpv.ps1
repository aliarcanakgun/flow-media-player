$ProgressPreference = 'SilentlyContinue'

$repo = "shinchiro/mpv-winbuild-cmake"
$apiUrl = "https://api.github.com/repos/$repo/releases/latest"

try {
    $release = Invoke-RestMethod -Uri $apiUrl
    $asset = $release.assets | Where-Object { $_.name -like "mpv-dev-x86_64-*.7z" } | Select-Object -First 1

    if (-not $asset) {
        Write-Error "Could not find mpv-dev-x86_64 asset in the latest release."
        exit 1
    }

    $downloadUrl = $asset.browser_download_url
    $outFile = "mpv-dev.7z"
    $outDir = "libmpv"

    Write-Host "Downloading $($asset.name) from $downloadUrl..."
    Invoke-WebRequest -Uri $downloadUrl -OutFile $outFile

    Write-Host "Extracting to $outDir..."
    if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    
    # 7z is usually in path or in C:\Program Files\7-Zip\7z.exe
    $7z = "7z"
    if (-not (Get-Command $7z -ErrorAction SilentlyContinue)) {
        if (Test-Path "C:\Program Files\7-Zip\7z.exe") {
            $7z = "C:\Program Files\7-Zip\7z.exe"
        } else {
            Write-Error "7z not found. Cannot extract."
            exit 1
        }
    }
    
    & $7z x $outFile -o$outDir -y
    
    Write-Host "Done! libmpv is ready."
} catch {
    Write-Error "Failed to download mpv: $_"
    exit 1
}
