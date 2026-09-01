[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [ValidateRange(1, 256)]
    [int[]]$SystemCounts = @(1, 2, 4, 8, 16, 32, 64, 128, 256),

    [ValidateRange(0, 1000000)]
    [int]$Stars = 500,

    [uint32]$Seed = 20260831,

    [ValidateRange(320, 16384)]
    [int]$Width = 1280,

    [ValidateRange(240, 16384)]
    [int]$Height = 720,

    [ValidateRange(1, 100)]
    [int]$Runs = 5,

    [ValidateRange(1, 1000)]
    [int]$TargetFps = 60,

    [string]$MakeCommand = "mingw32-make",

    [string]$OutputDirectory = "benchmark-results"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot "screensaver.exe"
$resultsPath = Join-Path $projectRoot $OutputDirectory
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SystemCounts = @($SystemCounts | Sort-Object -Unique)

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
    Invoke-CheckedCommand -Command $MakeCommand | Out-Host

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

    $rows = @()
    foreach ($systems in $SystemCounts) {
        for ($run = 1; $run -le $Runs; $run++) {
            Write-Host "Sistemas $systems, corrida $run de $Runs"
            $arguments = @(
                "$systems",
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
                throw "La corrida $run con $systems sistemas produjo $($benchmarkLines.Count) lineas BENCHMARK_CSV."
            }

            $fields = $benchmarkLines[0] -split ","
            if ($fields.Count -ne 13) {
                throw "La corrida $run con $systems sistemas produjo una linea BENCHMARK_CSV invalida."
            }
            if ([int]$fields[12] -ne 10) {
                throw "La corrida $run con $systems sistemas no produjo 10 intervalos completos."
            }

            $oneSecondMinFps = [double]::Parse(
                $fields[9], [Globalization.CultureInfo]::InvariantCulture)
            $rows += [pscustomobject]@{
                Run = $run
                Timestamp = (Get-Date).ToString("o")
                Commit = $commit
                DirtyWorkingTree = $dirty
                Cpu = $cpu
                Gpu = $gpu
                OperatingSystem = $os
                PowerPlan = $powerPlan
                Gcc = $gcc
                Seed = [uint32]$fields[1]
                Systems = [int]$fields[2]
                Stars = [int]$fields[3]
                Width = [int]$fields[4]
                Height = [int]$fields[5]
                Seconds = [double]::Parse($fields[6], [Globalization.CultureInfo]::InvariantCulture)
                Frames = [long]$fields[7]
                AverageFps = [double]::Parse($fields[8], [Globalization.CultureInfo]::InvariantCulture)
                OneSecondMinFps = $oneSecondMinFps
                GetFpsAverage = [double]::Parse($fields[10], [Globalization.CultureInfo]::InvariantCulture)
                GetFpsMin = [int]$fields[11]
                Samples = [int]$fields[12]
                TargetFps = $TargetFps
                Stable = $oneSecondMinFps -ge $TargetFps
            }
        }
    }

    New-Item -ItemType Directory -Path $resultsPath -Force | Out-Null
    $runsFile = Join-Path $resultsPath "runs-$timestamp.csv"
    $summaryFile = Join-Path $resultsPath "summary-$timestamp.csv"
    $hardwareFile = Join-Path $resultsPath "hardware-$timestamp.json"
    $rows | Export-Csv -LiteralPath $runsFile -NoTypeInformation -Encoding utf8
    $hardware | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $hardwareFile -Encoding utf8

    $summaryRows = foreach ($group in $rows | Group-Object Systems | Sort-Object { [int]$_.Name }) {
        $fps = @($group.Group | ForEach-Object { [double]$_.AverageFps } | Sort-Object)
        $mean = ($fps | Measure-Object -Average).Average
        if ($fps.Count % 2 -eq 0) {
            $middle = $fps.Count / 2
            $median = ($fps[$middle - 1] + $fps[$middle]) / 2.0
        } else {
            $median = $fps[[math]::Floor($fps.Count / 2)]
        }
        $variance = ($fps | ForEach-Object { [math]::Pow($_ - $mean, 2) } |
            Measure-Object -Average).Average
        $stableRuns = @($group.Group | Where-Object Stable).Count

        [pscustomobject]@{
            Systems = [int]$group.Name
            Runs = $Runs
            TargetFps = $TargetFps
            StableRuns = $stableRuns
            Stable = $stableRuns -eq $Runs
            MeanFps = [math]::Round($mean, 3)
            MedianFps = [math]::Round($median, 3)
            WorstOneSecondFps = [math]::Round(
                ($group.Group.OneSecondMinFps | Measure-Object -Minimum).Minimum, 3)
            StandardDeviationFps = [math]::Round([math]::Sqrt($variance), 3)
            RunsFile = $runsFile
            HardwareFile = $hardwareFile
        }
    }
    $summaryRows | Export-Csv -LiteralPath $summaryFile -NoTypeInformation -Encoding utf8

    $maxStable = @($summaryRows | Where-Object Stable | Sort-Object Systems)[-1]
    if ($null -eq $maxStable) {
        Write-Host "Ningun input medido mantuvo $TargetFps FPS en todas las corridas."
    } else {
        Write-Host "Mayor input estable medido: $($maxStable.Systems) sistemas a $TargetFps FPS."
    }
    Write-Host "Resultados: $runsFile"
    Write-Host "Resumen: $summaryFile"
    Write-Host "Hardware: $hardwareFile"
}
finally {
    Pop-Location
}
