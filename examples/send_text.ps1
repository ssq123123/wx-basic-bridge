param(
    [Parameter(Mandatory = $true)]
    [string]$To,
    [Parameter(Mandatory = $true)]
    [string]$Content,
    [int]$TimeoutSec = 20
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
python (Join-Path $Root "python\tools\wx_basic_bridge.py") --root $Root send $To $Content --timeout $TimeoutSec
