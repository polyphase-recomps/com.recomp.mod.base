# Copies this package (com.recomp.mod.base) into the other recomp projects' Packages
# folders, so every runtime builds against the same mod layer. Until the package lives in
# its own git repository (package.json dependency URL), this copy is the source of truth.
#
#   .\sync_modbase.ps1                     # the known recomp projects next to this one
#   .\sync_modbase.ps1 -Projects C:\x\MyProject, D:\y\Other
param([string[]]$Projects = @())
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ($Projects.Count -eq 0) {
    $recomp = (Resolve-Path (Join-Path $package '..\..\..\..\..')).Path   # ...\Recomp
    $Projects = @(
        "$recomp\LSD-DreamEmulator\LSD-DreamSimulator",
        "$recomp\StarFoxAdventures\Code\StarfoxAdventures",
        "$recomp\SuperSmashBros\Code\SuperSmashBros",
        "$recomp\KH-ChainOfMemories\Code\Kingdom Hearts - Chain Of Memories"
    )
}
foreach ($project in $Projects) {
    $packages = Join-Path $project 'Packages'
    if (-not (Test-Path $packages)) {
        Write-Host "skip (no Packages folder): $project"
        continue
    }
    $dest = Join-Path $packages 'com.recomp.mod.base'
    if ((Resolve-Path $packages).Path -eq (Resolve-Path (Join-Path $package '..')).Path) { continue }
    # mirror, without build output
    & robocopy $package $dest /MIR /XD build .git /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE) for $dest" }
    Write-Host "synced: $dest"
}
exit 0 # robocopy's 1-7 mean success
