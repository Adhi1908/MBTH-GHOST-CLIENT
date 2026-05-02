@echo off
"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "%~dp0fusion-plus\fusion-plus.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v142 /m /nologo /v:minimal
