param(
    [string]$Database = ""
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Args = @((Join-Path $Root "python\tools\wx_basic_bridge.py"), "--root", $Root, "watch")
if ($Database) { $Args += @("--db", $Database) }
python @Args
