param(
    [string]$OutFile = "",
    [switch]$SyntaxOnly
)

$ErrorActionPreference = "Stop"
$NativeRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $NativeRoot "..\..")
$IncludeDir = Join-Path $NativeRoot "src\include"
$BuildDir = Join-Path $NativeRoot "build"

$Sources = @(
    (Join-Path $NativeRoot "src\wx_hook_bridge.c"),
    (Join-Path $NativeRoot "src\modules\runtime.c"),
    (Join-Path $NativeRoot "src\modules\paths.c"),
    (Join-Path $NativeRoot "src\modules\memory.c"),
    (Join-Path $NativeRoot "src\modules\text.c"),
    (Join-Path $NativeRoot "src\modules\file_io.c"),
    (Join-Path $NativeRoot "src\modules\wechat_string.c"),
    (Join-Path $NativeRoot "src\modules\protocol.c"),
    (Join-Path $NativeRoot "src\modules\owner.c"),
    (Join-Path $NativeRoot "src\modules\send_runtime.c"),
    (Join-Path $NativeRoot "src\modules\send_text.c"),
    (Join-Path $NativeRoot "src\modules\receive.c"),
    (Join-Path $NativeRoot "src\modules\outbox.c"),
    (Join-Path $NativeRoot "src\modules\lifecycle.c")
)

if (-not $OutFile) { $OutFile = Join-Path $RepoRoot "dist\wx_hook_bridge.dll" }
$OutFile = [System.IO.Path]::GetFullPath($OutFile)
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($OutFile)) | Out-Null

$candidates = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)
$vcvars = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) {
    foreach ($root in @("C:\Program Files\Microsoft Visual Studio", "C:\Program Files (x86)\Microsoft Visual Studio")) {
        if (Test-Path $root) {
            $vcvars = Get-ChildItem -Path $root -Recurse -Filter vcvars64.bat -ErrorAction SilentlyContinue |
                Select-Object -First 1 -ExpandProperty FullName
            if ($vcvars) { break }
        }
    }
}
if (-not $vcvars) { throw "vcvars64.bat not found; install VS 2022 C++ Build Tools." }

$objDir = Join-Path $BuildDir ("obj_" + $PID)
New-Item -ItemType Directory -Force -Path $objDir | Out-Null
$objects = @()
$buildExitCode = 0
try {
    foreach ($source in $Sources) {
        $obj = Join-Path $objDir ([System.IO.Path]::GetFileNameWithoutExtension($source) + ".obj")
        $objects += $obj
        $compile = "cl /c /utf-8 /O2 /GS- /nologo /I:`"$IncludeDir`" /Fo:`"$obj`" `"$source`""
        & cmd /c "call `"$vcvars`" >nul 2>&1 && $compile"
        if ($LASTEXITCODE -ne 0) {
            $buildExitCode = $LASTEXITCODE
            break
        }
    }

    if ($buildExitCode -eq 0 -and -not $SyntaxOnly) {
        $quoted = ($objects | ForEach-Object { "`"$_`"" }) -join " "
        $importLibrary = Join-Path $objDir "wx_hook_bridge.lib"
        $link = "link /DLL /NOLOGO /OUT:`"$OutFile`" /IMPLIB:`"$importLibrary`" $quoted kernel32.lib user32.lib"
        & cmd /c "call `"$vcvars`" >nul 2>&1 && $link"
        if ($LASTEXITCODE -ne 0) {
            $buildExitCode = $LASTEXITCODE
        } else {
            Write-Host "built: $OutFile"
        }
    }
} finally {
    if (Test-Path -LiteralPath $objDir) {
        Remove-Item -LiteralPath $objDir -Recurse -Force
    }
    if ((Test-Path -LiteralPath $BuildDir) -and
        -not (Get-ChildItem -LiteralPath $BuildDir -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $BuildDir -Force
    }
}

if ($buildExitCode -ne 0) { exit $buildExitCode }
