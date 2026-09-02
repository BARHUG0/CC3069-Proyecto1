[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [ValidateRange(1, 1000)]
    [int[]]$TargetFpsValues = @(10, 30, 60, 90, 120),

    [ValidateRange(0, 1000000)]
    [int]$Stars = 500,

    [ValidateRange(1, 256)]
    [int]$MaxSystems = 256,

    [uint32]$Seed = 20260831,

    [ValidateRange(320, 16384)]
    [int]$Width = 1280,

    [ValidateRange(240, 16384)]
    [int]$Height = 720,

    [ValidateRange(1, 100)]
    [int]$Runs = 5,

    [ValidateSet("stability", "speedup")]
    [string]$Mode = "stability",

    [ValidateNotNullOrEmpty()]
    [ValidateRange(1, 256)]
    [int[]]$SystemsValues = @(1, 32, 64, 128, 256),

    [string]$MakeCommand = "mingw32-make",

    [ValidateSet("sequential", "parallel")]
    [string]$Version = "sequential",

    [ValidateRange(0, 4)]
    [int]$Threads = 0,

    [string]$OutputDirectory = "benchmark-results",

    [string]$ResumeRunsFile
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$executableName = if ($Version -eq "parallel") {
    "screensaver_parallel.exe"
} else {
    "screensaver.exe"
}
$executable = Join-Path $projectRoot $executableName
$resultsPath = Join-Path $projectRoot $OutputDirectory
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$TargetFpsValues = @($TargetFpsValues | Sort-Object -Unique)
$SystemsValues = @($SystemsValues | Sort-Object -Unique)
$previousOmpThreads = $env:OMP_NUM_THREADS
if ($Version -eq "parallel" -and $Threads -gt 0) {
    $env:OMP_NUM_THREADS = "$Threads"
}

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory)]
        [string]$Command,

        [string[]]$Arguments = @()
    )

    $output = & $Command @Arguments 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) {
        throw "'$Command $($Arguments -join ' ')' termino con codigo $LASTEXITCODE.`n$($output -join "`n")"
    }
    return $output
}

Push-Location $projectRoot
try {
    Invoke-CheckedCommand -Command $MakeCommand -Arguments @($Version) | Out-Host

    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "No se encontro el ejecutable compilado: $executable"
    }

    $commit = @(Invoke-CheckedCommand -Command "git" -Arguments @("rev-parse", "HEAD"))[0]
    $dirty = (Invoke-CheckedCommand -Command "git" -Arguments @("status", "--porcelain")).Count -gt 0
    $gcc = (Invoke-CheckedCommand -Command "gcc" -Arguments @("--version"))[0]
    $processors = @(Get-CimInstance Win32_Processor)
    $videoControllers = @(Get-CimInstance Win32_VideoController)
    $operatingSystem = Get-CimInstance Win32_OperatingSystem
    $cpu = ($processors | Select-Object -ExpandProperty Name) -join "; "
    $gpu = ($videoControllers |
        ForEach-Object { "$($_.Name) [$($_.DriverVersion)]" }) -join "; "
    $os = "$($operatingSystem.Caption) $($operatingSystem.Version)"
    $powerPlan = (& powercfg.exe /getactivescheme 2>&1 | ForEach-Object { "$_" }) -join " "
    $hardware = [ordered]@{
        Processors = @($processors | ForEach-Object {
            [ordered]@{
                Name = $_.Name
                Manufacturer = $_.Manufacturer
                PhysicalCores = $_.NumberOfCores
                LogicalProcessors = $_.NumberOfLogicalProcessors
                MaxClockMHz = $_.MaxClockSpeed
            }
        })
        Graphics = @($videoControllers | ForEach-Object {
            [ordered]@{
                Name = $_.Name
                VideoProcessor = $_.VideoProcessor
                DriverVersion = $_.DriverVersion
                CurrentResolution = "$($_.CurrentHorizontalResolution)x$($_.CurrentVerticalResolution)"
                CurrentRefreshRateHz = $_.CurrentRefreshRate
            }
        })
    }

    New-Item -ItemType Directory -Path $resultsPath -Force | Out-Null
    $runsFile = Join-Path $resultsPath "runs-$Mode-$Version-$timestamp.csv"
    $summaryFile = Join-Path $resultsPath "summary-$Mode-$Version-$timestamp.csv"
    $stabilityFile = Join-Path $resultsPath "stability-points-$Version-$timestamp.csv"
    $hardwareFile = Join-Path $resultsPath "hardware-$Mode-$Version-$timestamp.json"
    $hardware | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $hardwareFile -Encoding utf8

    $rows = [Collections.Generic.List[object]]::new()
    if ($ResumeRunsFile) {
        $resumePath = (Resolve-Path -LiteralPath $ResumeRunsFile).Path
        foreach ($row in Import-Csv -LiteralPath $resumePath) {
            if ([int]$row.Stars -ne $Stars -or [int]$row.Width -ne $Width -or
                [int]$row.Height -ne $Height -or [uint32]$row.Seed -ne $Seed -or
                "$($row.Version)" -ne $Version -or "$($row.Mode)" -ne $Mode) {
                throw "El archivo de reanudacion no coincide con version, estrellas, resolucion o semilla."
            }
            if ($Mode -eq "speedup" -and
                $SystemsValues -notcontains [int]$row.Systems) {
                throw "El archivo de reanudacion contiene una carga fuera de SystemsValues."
            }
            if ($Version -eq "parallel" -and $Threads -gt 0 -and
                [int]$row.Threads -ne $Threads) {
                throw "El archivo de reanudacion usa otra cantidad de hilos."
            }
            $rows.Add($row)
        }
        Write-Host "Se cargaron $($rows.Count) corridas desde $resumePath"
    }
    $measurements = @{}

    function Update-Measurement {
        param(
            [Parameter(Mandatory)]
            [int]$Systems
        )

        $systemRows = @($rows | Where-Object Systems -eq $Systems)
        $fps = @($systemRows | ForEach-Object { [double]$_.AverageFps } | Sort-Object)
        $mean = ($fps | Measure-Object -Average).Average
        if ($fps.Count % 2 -eq 0) {
            $middle = $fps.Count / 2
            $median = ($fps[$middle - 1] + $fps[$middle]) / 2.0
        } else {
            $median = $fps[[math]::Floor($fps.Count / 2)]
        }
        $variance = ($fps | ForEach-Object { [math]::Pow($_ - $mean, 2) } |
            Measure-Object -Average).Average
        $updateMs = @($systemRows | ForEach-Object { [double]$_.UpdateMs } | Sort-Object)
        $updateMean = ($updateMs | Measure-Object -Average).Average
        if ($updateMs.Count % 2 -eq 0) {
            $middle = $updateMs.Count / 2
            $updateMedian = ($updateMs[$middle - 1] + $updateMs[$middle]) / 2.0
        } else {
            $updateMedian = $updateMs[[math]::Floor($updateMs.Count / 2)]
        }
        $updateVariance = ($updateMs |
            ForEach-Object { [math]::Pow($_ - $updateMean, 2) } |
            Measure-Object -Average).Average
        $measurement = [pscustomobject]@{
            Mode = "$($systemRows[0].Mode)"
            Version = "$($systemRows[0].Version)"
            Threads = [int]$systemRows[0].Threads
            Systems = $Systems
            Stars = [int]$systemRows[0].Stars
            Width = [int]$systemRows[0].Width
            Height = [int]$systemRows[0].Height
            Seed = [uint32]$systemRows[0].Seed
            Runs = $systemRows.Count
            MeanFps = [math]::Round($mean, 3)
            MedianFps = [math]::Round($median, 3)
            WorstOneSecondFps = [math]::Round(
                ($systemRows.OneSecondMinFps | Measure-Object -Minimum).Minimum, 3)
            StandardDeviationFps = [math]::Round([math]::Sqrt($variance), 3)
            MeanUpdateMs = [math]::Round($updateMean, 6)
            MedianUpdateMs = [math]::Round($updateMedian, 6)
            StandardDeviationUpdateMs = [math]::Round([math]::Sqrt($updateVariance), 6)
        }
        $measurements[$Systems] = $measurement
        return $measurement
    }

    function Measure-Systems {
        param(
            [Parameter(Mandatory)]
            [int]$Systems,

            [switch]$AdditionalRuns
        )

        if ($measurements.ContainsKey($Systems) -and -not $AdditionalRuns) {
            return $measurements[$Systems]
        }

        $firstRun = @($rows | Where-Object Systems -eq $Systems).Count + 1
        $acceptedRuns = 0
        $attempts = 0
        while ($acceptedRuns -lt $Runs) {
            $attempts++
            if ($attempts -gt $Runs * 3) {
                throw "No se pudieron completar $Runs corridas validas con $Systems sistemas."
            }
            $run = $firstRun + $acceptedRuns
            Write-Host "Sistemas $Systems, corrida $($acceptedRuns + 1) de $Runs"
            $arguments = @(
                "$Systems",
                "--stars", "$Stars",
                "--seed", "$Seed",
                "--width", "$Width",
                "--height", "$Height",
                "--no-vsync",
                "--benchmark"
            )
            $output = Invoke-CheckedCommand -Command $executable -Arguments $arguments
            $benchmarkLines = @($output | Where-Object { $_ -like "BENCHMARK_CSV,*" })

            if ($benchmarkLines.Count -ne 1) {
                throw "La corrida $run con $Systems sistemas produjo $($benchmarkLines.Count) lineas BENCHMARK_CSV."
            }

            $fields = $benchmarkLines[0] -split ","
            if ($fields.Count -ne 16) {
                throw "La corrida $run con $Systems sistemas produjo una linea BENCHMARK_CSV invalida."
            }
            if ([int]$fields[14] -ne 10) {
                Write-Warning "La corrida $run con $Systems sistemas produjo $($fields[14]) intervalos; se repetira."
                continue
            }

            $oneSecondMinFps = [double]::Parse(
                $fields[11], [Globalization.CultureInfo]::InvariantCulture)
            $rows.Add([pscustomobject]@{
                Run = $run
                Timestamp = (Get-Date).ToString("o")
                Commit = $commit
                DirtyWorkingTree = $dirty
                Cpu = $cpu
                Gpu = $gpu
                OperatingSystem = $os
                PowerPlan = $powerPlan
                Gcc = $gcc
                Mode = $Mode
                Version = $fields[1]
                Threads = [int]$fields[2]
                Seed = [uint32]$fields[3]
                Systems = [int]$fields[4]
                Stars = [int]$fields[5]
                Width = [int]$fields[6]
                Height = [int]$fields[7]
                Seconds = [double]::Parse($fields[8], [Globalization.CultureInfo]::InvariantCulture)
                Frames = [long]$fields[9]
                AverageFps = [double]::Parse($fields[10], [Globalization.CultureInfo]::InvariantCulture)
                OneSecondMinFps = $oneSecondMinFps
                GetFpsAverage = [double]::Parse($fields[12], [Globalization.CultureInfo]::InvariantCulture)
                GetFpsMin = [int]$fields[13]
                Samples = [int]$fields[14]
                UpdateMs = [double]::Parse($fields[15], [Globalization.CultureInfo]::InvariantCulture)
            })
            $acceptedRuns++
        }

        $rows | Export-Csv -LiteralPath $runsFile -NoTypeInformation -Encoding utf8
        return Update-Measurement -Systems $Systems
    }

    foreach ($systems in @($rows |
        ForEach-Object { [int]$_.Systems } |
        Where-Object { $_ -ge 1 } |
        Sort-Object -Unique)) {
        Update-Measurement -Systems $systems | Out-Null
    }

    function Find-StabilityPoints {
        foreach ($targetFps in $TargetFpsValues) {
            Write-Host "Buscando limite estable para $targetFps FPS"
            $low = 0
            $high = $MaxSystems + 1
            $maximum = Measure-Systems -Systems $MaxSystems
            if ($maximum.WorstOneSecondFps -ge $targetFps) {
                $low = $MaxSystems
            } else {
                $high = $MaxSystems
                while ($high - $low -gt 1) {
                    $candidate = [math]::Floor(($low + $high) / 2)
                    $measurement = Measure-Systems -Systems $candidate
                    if ($measurement.WorstOneSecondFps -ge $targetFps) {
                        $low = $candidate
                    } else {
                        $high = $candidate
                    }
                }
            }

            $stableMeasurement = if ($low -gt 0) {
                Measure-Systems -Systems $low
            } else {
                $null
            }
            $unstableMeasurement = if ($high -le $MaxSystems) {
                Measure-Systems -Systems $high
            } else {
                $null
            }

            [pscustomobject]@{
                Version = $Version
                Threads = if ($Version -eq "parallel") {
                    $maximum.Threads
                } else {
                    1
                }
                TargetFps = $targetFps
                MaxStableSystems = $low
                Stars = $Stars
                WorstOneSecondFps = if ($null -ne $stableMeasurement) {
                    $stableMeasurement.WorstOneSecondFps
                } else {
                    $null
                }
                Runs = if ($null -ne $stableMeasurement) {
                    $stableMeasurement.Runs
                } else {
                    $Runs
                }
                Width = $Width
                Height = $Height
                Seed = $Seed
                FirstUnstableSystems = if ($null -ne $unstableMeasurement) {
                    $unstableMeasurement.Systems
                } else {
                    $null
                }
                FirstUnstableWorstOneSecondFps = if ($null -ne $unstableMeasurement) {
                    $unstableMeasurement.WorstOneSecondFps
                } else {
                    $null
                }
                RunsFile = $runsFile
                SummaryFile = $summaryFile
                HardwareFile = $hardwareFile
            }
        }
    }

    $stabilityRows = @()
    if ($Mode -eq "speedup") {
        foreach ($systems in $SystemsValues) {
            Measure-Systems -Systems $systems | Out-Null
        }
    } else {
        $stabilityRows = @(Find-StabilityPoints)
        $conflicts = @()
        foreach ($targetFps in $TargetFpsValues) {
            $ordered = @($measurements.Values |
                Where-Object { [int]$_.Systems -ge 1 } |
                Sort-Object Systems)
            for ($i = 0; $i -lt $ordered.Count; $i++) {
                for ($j = $i + 1; $j -lt $ordered.Count; $j++) {
                    if ($ordered[$i].WorstOneSecondFps -lt $targetFps -and
                        $ordered[$j].WorstOneSecondFps -ge $targetFps) {
                        $conflicts += $ordered[$i].Systems
                        $conflicts += $ordered[$j].Systems
                    }
                }
            }
        }

        if ($conflicts.Count -gt 0) {
            $conflicts = @($conflicts |
                Where-Object { [int]$_ -ge 1 } |
                Sort-Object -Unique)
            Write-Host "Repitiendo puntos con resultados no monotonicos: $($conflicts -join ', ')"
            foreach ($systems in $conflicts) {
                Measure-Systems -Systems $systems -AdditionalRuns | Out-Null
            }
            $stabilityRows = @(Find-StabilityPoints)
        }
    }

    $summaryRows = @($measurements.Values | Sort-Object Systems | ForEach-Object {
        [pscustomobject]@{
            Mode = $_.Mode
            Version = $_.Version
            Threads = $_.Threads
            Systems = $_.Systems
            Stars = $_.Stars
            Width = $_.Width
            Height = $_.Height
            Seed = $_.Seed
            Runs = $_.Runs
            MeanFps = $_.MeanFps
            MedianFps = $_.MedianFps
            WorstOneSecondFps = $_.WorstOneSecondFps
            StandardDeviationFps = $_.StandardDeviationFps
            MeanUpdateMs = $_.MeanUpdateMs
            MedianUpdateMs = $_.MedianUpdateMs
            StandardDeviationUpdateMs = $_.StandardDeviationUpdateMs
            RunsFile = $runsFile
            HardwareFile = $hardwareFile
        }
    })
    $summaryRows | Export-Csv -LiteralPath $summaryFile -NoTypeInformation -Encoding utf8

    if ($Mode -eq "stability") {
        $stabilityRows | Export-Csv -LiteralPath $stabilityFile -NoTypeInformation -Encoding utf8
        $stabilityRows | Format-Table TargetFps, MaxStableSystems, Stars, WorstOneSecondFps
        Write-Host "Puntos de estabilidad: $stabilityFile"
    } else {
        $summaryRows | Format-Table Systems, MeanUpdateMs, MedianUpdateMs, MeanFps
    }
    Write-Host "Resultados: $runsFile"
    Write-Host "Resumen: $summaryFile"
    Write-Host "Hardware: $hardwareFile"
}
finally {
    Pop-Location
    $env:OMP_NUM_THREADS = $previousOmpThreads
}
