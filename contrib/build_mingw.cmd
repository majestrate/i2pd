@echo off
title ���ઠ i2pd

set "WD=C:\msys64"
set CHERE_INVOKING=enabled_from_arguments
set MSYSCON=mintty.exe

echo ���ઠ i2pd ��� win32. ������ Enter ��᫥ ����砭�� �������樨...
set "MSYSTEM=MINGW32"
set "CONTITLE=MinGW x32"
start "%CONTITLE%" /WAIT C:\msys64\usr\bin\mintty.exe -i /msys2.ico /usr/bin/bash --login build_mingw.sh
pause

echo ���ઠ i2pd ��� win64. ������ Enter ��᫥ ����砭�� �������樨...
set "MSYSTEM=MINGW64"
set "CONTITLE=MinGW x64"
start "%CONTITLE%" /WAIT C:\msys64\usr\bin\mintty.exe -i /msys2.ico /usr/bin/bash --login build_mingw.sh
pause

echo ���ઠ �����襭�...
pause
exit /b 0