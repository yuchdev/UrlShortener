<#
.SYNOPSIS
    Sets up crash dump collection for url_shortener.exe on Windows.

.DESCRIPTION
    Step 1 - WER LocalDumps: registry config (admin/UAC) for SEH-based crashes.
    Step 2 - ProcDump: downloads Sysinternals ProcDump to C:\Tools.
    ProcDump is required for STATUS_FAST_FAIL (0xC0000409), which bypasses WER.

.NOTES
    Run without -SkipWer to configure both steps (UAC prompt for Step 1).
    Run with -SkipWer for ProcDump only (no UAC prompt needed).

.EXAMPLE
    .\setup_dump_collection.ps1
    .\setup_dump_collection.ps1 -DumpDir D:\Dumps -SkipWer
#>

[CmdletBinding()]
param(
    [string]$DumpDir  = "$env:LOCALAPPDATA\CrashDumps",
    [string]$ToolsDir = "C:\Tools",
    [int]$DumpCount   = 5,
    [switch]$SkipWer
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ExeName    = "url_shortener.exe"
$BinaryPath = Join-Path $PSScriptRoot "cmake-build\RelWithDebInfo\$ExeName"

function Write-Step([string]$m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Write-Ok([string]$m)   { Write-Host "    OK   $m" -ForegroundColor Green }
function Write-Warn([string]$m) { Write-Host "    WARN $m" -ForegroundColor Yellow }
function Write-Fail([string]$m) { Write-Host "    FAIL $m" -ForegroundColor Red }

# ---------------------------------------------------------------------------
# Step 1 - WER LocalDumps
# ---------------------------------------------------------------------------
if (-not $SkipWer) {
    Write-Step "Configuring WER LocalDumps for $ExeName"

    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal   = New-Object Security.Principal.WindowsPrincipal($currentUser)
    $isAdmin     = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

    $regKey = "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\$ExeName"

    $werCmds = @(
        "New-Item -Path '$regKey' -Force | Out-Null",
        "Set-ItemProperty '$regKey' -Name DumpFolder -Value '$DumpDir'",
        "Set-ItemProperty '$regKey' -Name DumpType   -Value 2",
        "Set-ItemProperty '$regKey' -Name DumpCount  -Value $DumpCount",
        "New-Item -Path '$DumpDir' -ItemType Directory -Force | Out-Null",
        "Write-Host 'WER LocalDumps configured.'"
    )
    $werScript = $werCmds -join [Environment]::NewLine

    if ($isAdmin) {
        Invoke-Expression $werScript
    } else {
        Write-Warn "Not admin - launching elevated UAC prompt for WER step"
        $tmpScript = Join-Path $env:TEMP "wer_setup_urlshortener.ps1"
        Set-Content -Path $tmpScript -Value $werScript -Encoding UTF8
        Start-Process powershell.exe `
            -ArgumentList "-ExecutionPolicy Bypass -NoProfile -File `"$tmpScript`"" `
            -Verb RunAs -Wait
        Remove-Item $tmpScript -Force -ErrorAction SilentlyContinue
    }

    if (Test-Path $regKey) {
        $p = Get-ItemProperty $regKey
        Write-Ok "WER registry key created"
        Write-Ok "  DumpFolder : $($p.DumpFolder)"
        Write-Ok "  DumpType   : $($p.DumpType)  (2 = full minidump)"
        Write-Ok "  DumpCount  : $($p.DumpCount)"
    } else {
        Write-Warn "WER key not found - UAC may have been cancelled"
        Write-Warn "Re-run as Administrator, or use -SkipWer to skip this step"
    }

    Write-Warn "STATUS_FAST_FAIL (0xC0000409) bypasses WER - ProcDump (Step 2) handles it"
}

# ---------------------------------------------------------------------------
# Step 2 - ProcDump
# ---------------------------------------------------------------------------
Write-Step "Checking ProcDump installation"

$procdump = Join-Path $ToolsDir "procdump64.exe"

if (Test-Path $procdump) {
    Write-Ok "ProcDump already installed: $procdump"
} else {
    Write-Host "    Downloading ProcDump from Sysinternals..." -ForegroundColor Yellow
    New-Item -Path $ToolsDir -ItemType Directory -Force | Out-Null
    $zip = Join-Path $env:TEMP "Procdump_setup.zip"
    try {
        Invoke-WebRequest `
            -Uri "https://download.sysinternals.com/files/Procdump.zip" `
            -OutFile $zip -UseBasicParsing
        Expand-Archive -Path $zip -DestinationPath $ToolsDir -Force
        Remove-Item $zip -Force -ErrorAction SilentlyContinue
        Write-Ok "ProcDump installed to $ToolsDir"
    } catch {
        Write-Fail "Download failed: $_"
        Write-Fail "Manual: https://learn.microsoft.com/sysinternals/downloads/procdump"
        Write-Fail "Extract procdump64.exe to $ToolsDir and re-run"
    }
}

New-Item -Path $DumpDir -ItemType Directory -Force | Out-Null
Write-Ok "Dump output directory: $DumpDir"

# ---------------------------------------------------------------------------
# Step 3 - Print capture commands
# ---------------------------------------------------------------------------
Write-Step "Setup complete. Use these commands to capture crashes:"

Write-Host ""
Write-Host "  # Launch and capture (handles STATUS_FAST_FAIL and all other exceptions):" -ForegroundColor White
Write-Host "  $procdump -accepteula -e 1 -ma -x `"$DumpDir`" `"$BinaryPath`" `"--http-port`" `"8080`"" -ForegroundColor White
Write-Host ""
Write-Host "  # Attach to a running process by PID:" -ForegroundColor White
Write-Host "  $procdump -accepteula -e 1 -ma -x `"$DumpDir`" <PID>" -ForegroundColor White
Write-Host ""
Write-Host "  # List collected dumps:" -ForegroundColor White
Write-Host "  Get-ChildItem `"$DumpDir`" -Filter *.dmp | Select-Object Name, LastWriteTime" -ForegroundColor White
Write-Host ""
Write-Host "  See CRASH_DUMP_GUIDE.md for dump types and WER limitations." -ForegroundColor DarkGray