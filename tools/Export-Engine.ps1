param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$PublicRemote = 'https://github.com/D4rkks/cpp-gameengine.git'
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$targetRoot = (Resolve-Path -LiteralPath $Destination).Path
if ($sourceRoot.TrimEnd('\') -eq $targetRoot.TrimEnd('\')) {
    throw 'Use a separate clone of the public repository.'
}
$repoRoot = (& git -C $targetRoot rev-parse --show-toplevel)
if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($repoRoot) -ne [IO.Path]::GetFullPath($targetRoot)) {
    throw 'The destination must be the root of its own Git clone.'
}
$remote = (& git -C $targetRoot remote get-url origin)
if ($LASTEXITCODE -ne 0 -or $remote.TrimEnd('/') -ne $PublicRemote.TrimEnd('/')) {
    throw 'The destination origin does not match the expected public repository.'
}

# Only these engine-owned paths can cross the private/public boundary.
$trees = @('src/Engine', 'src/Scripts', 'assets', 'resources', 'cmake', 'docs', 'examples', 'tests', 'tools')
$files = @('src/main.cpp', 'src/GameRegistry.cpp', 'CMakeLists.txt', 'README.md', 'LICENSE')
$copied = 0
function Copy-EngineFile([string]$relative) {
    if ($relative -match '(^|[/\\])(games|\.git|\.cache|build|Build|node_modules)([/\\]|$)' -or
        $relative -match '\.(exe|dll|pdb|lib|zip|log|pyc)$' -or
        $relative -match '(^|[/\\])imgui\.(ini|recent)$') { return }
    $source = Join-Path $sourceRoot $relative
    $item = Get-Item -LiteralPath $source
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing link: $relative" }
    $target = Join-Path $targetRoot $relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    [IO.File]::Copy($source, $target, $true)
    $script:copied++
}
foreach ($relative in $files) {
    if (Test-Path -LiteralPath (Join-Path $sourceRoot $relative) -PathType Leaf) { Copy-EngineFile $relative }
}
foreach ($tree in $trees) {
    $folder = Join-Path $sourceRoot $tree
    if (!(Test-Path -LiteralPath $folder -PathType Container)) { continue }
    Get-ChildItem -LiteralPath $folder -File -Recurse | ForEach-Object {
        Copy-EngineFile $_.FullName.Substring($sourceRoot.Length + 1)
    }
}
# Additional protection in the public clone, without ignoring private game work
# in the source repository. This does not delete or untrack any existing files.
@'
/build*/
/games/
/.cache/
/.vs/
/.vscode/
**/.cache/
**/Build/
**/imgui.ini
**/imgui.recent
*.exe
*.dll
*.lib
*.pdb
*.o
*.zip
*.log
*.pyc
__pycache__/
CMakeCache.txt
CMakeFiles/
'@ | Set-Content -LiteralPath (Join-Path $targetRoot '.gitignore') -Encoding UTF8
Write-Output "Copied $copied engine files. No source Git history was transferred."
Write-Output 'Review the destination diff before committing and pushing. This script does not run either action.'
