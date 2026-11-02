# Locates MSVC's vcvars*.bat folder (dot-source, then call Get-VcVarsDir).
# Order: $env:VINPUT_VCVARS_DIR (a ...\VC\Auxiliary\Build folder), then vswhere's newest install with the C++ tools.
function Get-VcVarsDir {
  if ($env:VINPUT_VCVARS_DIR) {
    if (Test-Path -LiteralPath (Join-Path $env:VINPUT_VCVARS_DIR 'vcvarsall.bat')) { return $env:VINPUT_VCVARS_DIR }
    throw "VINPUT_VCVARS_DIR has no vcvarsall.bat: $env:VINPUT_VCVARS_DIR"
  }
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path -LiteralPath $vswhere) {
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if ($install) {
      $dir = Join-Path $install 'VC\Auxiliary\Build'
      if (Test-Path -LiteralPath (Join-Path $dir 'vcvarsall.bat')) { return $dir }
    }
  }
  throw 'MSVC not found: install Visual Studio (or Build Tools) with the C++ x64/x86 tools, or set VINPUT_VCVARS_DIR to its VC\Auxiliary\Build folder'
}
