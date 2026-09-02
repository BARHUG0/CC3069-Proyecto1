[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$SequentialSummary,

    [Parameter(Mandatory)]
    [string]$ParallelSummary,

    [string]$OutputFile
)

$ErrorActionPreference = "Stop"
$sequentialRows = @(Import-Csv -LiteralPath $SequentialSummary)
$parallelRows = @(Import-Csv -LiteralPath $ParallelSummary)

if ($sequentialRows.Count -eq 0 -or $parallelRows.Count -eq 0) {
    throw "Los dos archivos deben contener mediciones."
}
if (@($sequentialRows | Where-Object { $_.Mode -ne "speedup" -or $_.Version -ne "sequential" }).Count -gt 0) {
    throw "SequentialSummary debe provenir del modo speedup y de la version sequential."
}
if (@($parallelRows | Where-Object { $_.Mode -ne "speedup" -or $_.Version -ne "parallel" }).Count -gt 0) {
    throw "ParallelSummary debe provenir del modo speedup y de la version parallel."
}

$sequentialBySystems = @{}
foreach ($row in $sequentialRows) {
    if ([int]$row.Systems -ge 1) {
        $sequentialBySystems[[int]$row.Systems] = $row
    }
}

$parallelSystems = @($parallelRows |
    ForEach-Object { [int]$_.Systems } |
    Where-Object { $_ -ge 1 } |
    Sort-Object -Unique)
$sequentialSystems = @($sequentialBySystems.Keys | Sort-Object)
if (($parallelSystems -join ",") -ne ($sequentialSystems -join ",")) {
    throw "Los resumenes deben contener exactamente los mismos valores de N."
}

$comparison = @(foreach ($parallel in $parallelRows) {
    $systems = [int]$parallel.Systems
    if ($systems -lt 1) {
        continue
    }

    $sequential = $sequentialBySystems[$systems]
    foreach ($field in @("Stars", "Width", "Height", "Seed", "Runs")) {
        if ("$($sequential.$field)" -ne "$($parallel.$field)") {
            throw "Las mediciones para $systems sistemas no coinciden en $field."
        }
    }

    $sequentialUpdateMs = [double]$sequential.MeanUpdateMs
    $parallelUpdateMs = [double]$parallel.MeanUpdateMs
    $threads = [int]$parallel.Threads
    if ($sequentialUpdateMs -le 0.0 -or $parallelUpdateMs -le 0.0 -or $threads -lt 1) {
        throw "Las mediciones para $systems sistemas contienen tiempos o hilos invalidos."
    }
    $speedup = $sequentialUpdateMs / $parallelUpdateMs

    [pscustomobject]@{
        Systems = $systems
        Stars = [int]$parallel.Stars
        Threads = $threads
        SequentialMeanUpdateMs = $sequentialUpdateMs
        ParallelMeanUpdateMs = $parallelUpdateMs
        SequentialMeanFps = [double]$sequential.MeanFps
        ParallelMeanFps = [double]$parallel.MeanFps
        Speedup = [math]::Round($speedup, 4)
        EfficiencyPercent = [math]::Round(100.0 * $speedup / $threads, 2)
    }
})

if ($comparison.Count -eq 0) {
    throw "No hay cantidades de sistemas comunes entre los dos archivos."
}

if (-not $OutputFile) {
    $directory = Split-Path -Parent (Resolve-Path -LiteralPath $ParallelSummary)
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputFile = Join-Path $directory "comparison-$timestamp.csv"
}

$comparison | Sort-Object Systems |
    Export-Csv -LiteralPath $OutputFile -NoTypeInformation -Encoding utf8
$comparison | Sort-Object Systems |
    Format-Table Systems, Threads, SequentialMeanUpdateMs, ParallelMeanUpdateMs, Speedup, EfficiencyPercent
Write-Host "Comparacion: $OutputFile"
