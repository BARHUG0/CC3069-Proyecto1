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

$sequentialBySystems = @{}
foreach ($row in $sequentialRows) {
    if ([int]$row.Systems -ge 1) {
        $sequentialBySystems[[int]$row.Systems] = $row
    }
}

$comparison = @(foreach ($parallel in $parallelRows) {
    $systems = [int]$parallel.Systems
    if ($systems -lt 1 -or -not $sequentialBySystems.ContainsKey($systems)) {
        continue
    }

    $sequential = $sequentialBySystems[$systems]
    foreach ($field in @("Stars", "Width", "Height", "Seed")) {
        if ("$($sequential.$field)" -ne "$($parallel.$field)") {
            throw "Las mediciones para $systems sistemas no coinciden en $field."
        }
    }

    $sequentialFps = [double]$sequential.MeanFps
    $parallelFps = [double]$parallel.MeanFps
    $threads = [int]$parallel.Threads
    $speedup = $parallelFps / $sequentialFps

    [pscustomobject]@{
        Systems = $systems
        Stars = [int]$parallel.Stars
        Threads = $threads
        SequentialMeanFps = $sequentialFps
        ParallelMeanFps = $parallelFps
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
    Format-Table Systems, Threads, SequentialMeanFps, ParallelMeanFps, Speedup, EfficiencyPercent
Write-Host "Comparacion: $OutputFile"
