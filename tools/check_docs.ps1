# Doc lint for this repo (run from anywhere): tools\check_docs.ps1
#   1. broken markdown links / images (code spans and fenced code are ignored)
#   2. "§N" references into circular-design.md / circular-art-guide.md / circular-balance.md that name a missing section
#   3. identifiers of deleted code that still appear in prose (lines that say deleted/old/former are allowed)
# Exit code 1 when anything is reported. This file is saved as UTF-8 WITH BOM on purpose: Windows PowerShell 5.1 reads BOM-less
# files as ANSI and would mangle the Korean words and the section sign used in the patterns below.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$files = @(Get-ChildItem docs\*.md) + @(Get-Item README.md, CLAUDE.md, assets\README.md)
$problems = New-Object System.Collections.Generic.List[string]

function Strip-Code([string]$text) {
    $text = [regex]::Replace($text, '(?s)```.*?```', '')
    $text = [regex]::Replace($text, '`[^`\r\n]*`', '')
    return $text
}

# ---- 1. links -------------------------------------------------------------
foreach ($f in $files) {
    $text = Strip-Code ([IO.File]::ReadAllText($f.FullName))
    $dir = Split-Path $f.FullName
    foreach ($m in [regex]::Matches($text, '\]\((<[^>\r\n]+>|[^)\s]+)\)')) {
        $raw = $m.Groups[1].Value
        $angle = $raw.StartsWith('<')
        $target = $raw.Trim('<', '>')
        if ($target -match '^(https?:|mailto:|#)') { continue }
        if (-not $angle) { $target = ($target -split '#')[0] }
        $target = $target -replace ':\d+$', ''
        if ($target -eq '') { continue }
        if (-not (Test-Path -LiteralPath (Join-Path $dir $target))) {
            $problems.Add("LINK    $($f.Name): $target")
        }
    }
}

# ---- 2. section references ------------------------------------------------
function Get-Sections([string]$path) {
    $set = New-Object System.Collections.Generic.HashSet[string]
    foreach ($line in [IO.File]::ReadAllLines($path)) {
        if ($line -match '^#{2,4}\s+(\d+(?:\.\d+)?)[\.\s]') { [void]$set.Add($Matches[1]) }
    }
    return , $set
}
$targets = @{}
foreach ($name in 'circular-design.md', 'circular-art-guide.md', 'circular-balance.md') {
    $targets[$name] = Get-Sections (Join-Path $root "docs\$name")
}
foreach ($f in $files) {
    $text = Strip-Code ([IO.File]::ReadAllText($f.FullName))
    foreach ($name in $targets.Keys) {
        $pattern = [regex]::Escape("]($name)") + '\s*(?:\)|,|·)?\s*§(\d+(?:\.\d+)?)'
        foreach ($m in [regex]::Matches($text, $pattern)) {
            if (-not $targets[$name].Contains($m.Groups[1].Value)) {
                $problems.Add("SECTION $($f.Name): $name has no section $($m.Groups[1].Value)")
            }
        }
        # bare "§N" inside the document itself
        if ($f.Name -eq $name) {
            $own = [regex]::Replace($text, '\]\((?!' + [regex]::Escape($name) + ')[^)]*\)\s*(?:§\d+(?:\.\d+)?)?', '](x)')
            foreach ($m in [regex]::Matches($own, '(?<![\w])§(\d+(?:\.\d+)?)')) {
                if (-not $targets[$name].Contains($m.Groups[1].Value)) {
                    $problems.Add("SECTION $($f.Name): self reference to missing section $($m.Groups[1].Value)")
                }
            }
        }
    }
}

# ---- 3. deleted identifiers still mentioned as if alive ---------------------
$stale = 'EnterTitle', 'TitleScreen', 'BuildTitleScreen', 'InGameHud', 'BuildInGameHud', 'InventoryScreen',
         'BuildInventoryScreen', 'BuildDemoUiSprites', 'm_uiAtlas', 'onExitToTitle', 'EXIT TO TITLE', 'EnterItems',
         'kCircularAttack', 'spawnIntervalSeconds', 'm_circularAttackCooldown', 'm_mobSpawnTimer'
$allowed = '삭제|예전|옷|옛|deleted|removed|former'   # deleted / former / old (Korean via escapes)
foreach ($f in $files) {
    $text = [regex]::Replace([IO.File]::ReadAllText($f.FullName), '(?s)```.*?```', '')
    $n = 0
    foreach ($line in ($text -split "`n")) {
        $n++
        foreach ($s in $stale) {
            if ($line.Contains($s) -and ($line -notmatch $allowed)) {
                $problems.Add("STALE   $($f.Name)`:$n mentions '$s' without saying it was deleted")
            }
        }
    }
}

if ($problems.Count -eq 0) {
    Write-Host "check_docs: OK ($($files.Count) files)"
    exit 0
}
$problems | ForEach-Object { Write-Host $_ }
Write-Host "check_docs: $($problems.Count) problem(s)"
exit 1
