@echo off
REM ========================================
REM STM32 Bin文件生成脚本
REM 用于从Keil编译生成的axf文件转换为bin文件
REM ========================================

echo.
echo ========================================
echo STM32 Bin File Generator
echo ========================================
echo.

REM 设置Keil安装路径 (根据实际情况修改)
set KEIL_PATH=C:\Keil_v5\ARM\ARMCC\bin
set FROMELF=%KEIL_PATH%\fromelf.exe

REM 检查fromelf是否存在
if not exist "%FROMELF%" (
    echo Error: fromelf.exe not found!
    echo Please modify KEIL_PATH in this script.
    echo Current path: %KEIL_PATH%
    pause
    exit /b 1
)

REM 获取当前目录
set SCRIPT_DIR=%~dp0
set PROJECT_DIR=%SCRIPT_DIR%..

echo Project Directory: %PROJECT_DIR%
echo.

REM ========================================
REM 生成Bootloader.bin
REM ========================================
echo [1/3] Generating Bootloader.bin...
set BOOTLOADER_AXF=%PROJECT_DIR%\Objects\Bootloader.axf
set BOOTLOADER_BIN=%PROJECT_DIR%\Objects\bootloader.bin

if exist "%BOOTLOADER_AXF%" (
    "%FROMELF%" --bin --output="%BOOTLOADER_BIN%" "%BOOTLOADER_AXF%"
    if exist "%BOOTLOADER_BIN%" (
        echo Success: bootloader.bin created
        for %%A in ("%BOOTLOADER_BIN%") do echo Size: %%~zA bytes
    ) else (
        echo Failed to create bootloader.bin
    )
) else (
    echo Warning: Bootloader.axf not found, skipping...
)
echo.

REM ========================================
REM 生成APP1.bin
REM ========================================
echo [2/3] Generating APP1.bin...
set APP1_AXF=%PROJECT_DIR%\Objects\APP1.axf
set APP1_BIN=%PROJECT_DIR%\Objects\app1.bin

if exist "%APP1_AXF%" (
    "%FROMELF%" --bin --output="%APP1_BIN%" "%APP1_AXF%"
    if exist "%APP1_BIN%" (
        echo Success: app1.bin created
        for %%A in ("%APP1_BIN%") do echo Size: %%~zA bytes
    ) else (
        echo Failed to create app1.bin
    )
) else (
    echo Warning: APP1.axf not found, skipping...
)
echo.

REM ========================================
REM 生成APP2.bin
REM ========================================
echo [3/3] Generating APP2.bin...
set APP2_AXF=%PROJECT_DIR%\Objects\APP2.axf
set APP2_BIN=%PROJECT_DIR%\Objects\app2.bin

if exist "%APP2_AXF%" (
    "%FROMELF%" --bin --output="%APP2_BIN%" "%APP2_AXF%"
    if exist "%APP2_BIN%" (
        echo Success: app2.bin created
        for %%A in ("%APP2_BIN%") do echo Size: %%~zA bytes
    ) else (
        echo Failed to create app2.bin
    )
) else (
    echo Warning: APP2.axf not found, skipping...
)
echo.

REM ========================================
REM 检查文件大小
REM ========================================
echo ========================================
echo Size Check (Max: Bootloader=16KB, APP=24KB)
echo ========================================

if exist "%BOOTLOADER_BIN%" (
    for %%A in ("%BOOTLOADER_BIN%") do (
        set /a SIZE=%%~zA
        set /a MAX_SIZE=16384
        if %%~zA GTR 16384 (
            echo WARNING: bootloader.bin ^(%%~zA bytes^) exceeds 16KB limit!
        ) else (
            echo OK: bootloader.bin ^(%%~zA bytes^)
        )
    )
)

if exist "%APP1_BIN%" (
    for %%A in ("%APP1_BIN%") do (
        if %%~zA GTR 24576 (
            echo WARNING: app1.bin ^(%%~zA bytes^) exceeds 24KB limit!
        ) else (
            echo OK: app1.bin ^(%%~zA bytes^)
        )
    )
)

if exist "%APP2_BIN%" (
    for %%A in ("%APP2_BIN%") do (
        if %%~zA GTR 24576 (
            echo WARNING: app2.bin ^(%%~zA bytes^) exceeds 24KB limit!
        ) else (
            echo OK: app2.bin ^(%%~zA bytes^)
        )
    )
)

echo.
echo ========================================
echo Done!
echo ========================================
echo.
pause

