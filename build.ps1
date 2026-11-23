param([string]$Sdk = '')
$ErrorActionPreference='Stop'
if(-not (Test-Path (Join-Path $Sdk 'Ashita.h'))){throw 'Pass -Sdk with the Ashita v4 SDK directory'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'C++ x86 build tools unavailable'}
$vc=Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$build=Join-Path $PSScriptRoot 'build'
$dist=Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force $build,$dist | Out-Null
$objects=@()
foreach($unit in @('buffer','hook','trampoline','hde\hde32')) {
    $c=Join-Path $PSScriptRoot ('vendor\minhook\src\'+$unit+'.c')
    $o=Join-Path $build (($unit -replace '\\','_')+'.obj')
    cmd.exe /c "`"$vc`" >nul && cl /nologo /O2 /MT /W3 /c `"$c`" /Fo:`"$o`""
    if($LASTEXITCODE -ne 0){throw 'MinHook compile failed'}
    $objects+=('"'+$o+'"')
}
$linkObjects=$objects -join ' '
$dll=Join-Path $dist 'VanaSound.dll'
$source=Join-Path $PSScriptRoot 'src\VanaSound.cpp'
$obj=Join-Path $build 'mixer.obj'
cmd.exe /c "`"$vc`" >nul && cl /nologo /std:c++20 /EHa /O2 /MT /W4 /D_CRT_SECURE_NO_WARNINGS /I`"$Sdk`" /LD `"$source`" /Fo:`"$obj`" /Fe:`"$dll`" $linkObjects /link /Brepro"
if($LASTEXITCODE -ne 0){throw 'Mixer build failed'}
$source=Join-Path $PSScriptRoot 'tests\mixer.cpp'
$test=Join-Path $build 'mixer-test.exe'
$obj=Join-Path $build 'mixer-test.obj'
cmd.exe /c "`"$vc`" >nul && cl /nologo /std:c++20 /EHa /O2 /MT /W4 /D_CRT_SECURE_NO_WARNINGS /I`"$Sdk`" `"$source`" /Fo:`"$obj`" /Fe:`"$test`" $linkObjects /link /Brepro"
if($LASTEXITCODE -ne 0){throw 'Mixer test build failed'}
foreach($profile in @('0','1')) {
    & $test $dll $profile
    if($LASTEXITCODE -ne 0){throw 'Mixer tests failed'}
}
