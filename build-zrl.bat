@echo off
chcp 65001 >nul 2>&1
echo ========================================
echo   ZRL OS Build Script
echo ========================================
echo.
echo   Choose build method:
echo.
echo   [1] Docker (recommended, needs Docker Desktop)
echo   [2] WSL Native (slower, builds toolchain from source)
echo   [0] Exit
echo.

set /p CHOICE=Enter choice (0-2):

if "%CHOICE%"=="0" goto :eof
if "%CHOICE%"=="1" goto docker_build
if "%CHOICE%"=="2" goto wsl_build
echo Invalid choice.
pause
goto :eof

:docker_build
echo.
echo === Docker Build ===
cd /d "%~dp0"

echo [1/4] Checking submodules...
if not exist "kuroko\src\kuroko.c" (
    echo   Cloning kuroko...
    git clone --depth 1 https://github.com/kuroko-lang/kuroko.git kuroko
) else (echo   kuroko: OK)
if not exist "bim\bim.c" (
    echo   Cloning bim...
    git clone --depth 1 https://github.com/klange/bim.git bim
) else (echo   bim: OK)
echo.

echo [2/4] Pulling Docker image (if needed)...
docker pull toaruos/build-tools:1.99.x
echo.

echo [3/4] Building with Docker...
docker run --rm -v "%CD%":/root/misaka -w /root/misaka -e LANG=C.UTF-8 toaruos/build-tools:1.99.x util/build-in-docker.sh
echo.

echo [4/4] Results:
if exist "image.iso" (
    for %%A in ("image.iso") do set SIZE=%%~zA
    echo   SUCCESS! image.iso ^( %SIZE% bytes ^)
    echo   Run: qemu-system-x86_64 -m 1G -cdrom image.iso -enable-kvm
) else (echo   FAILED - image.iso not found)
pause
goto :eof

:wsl_build
echo.
echo === WSL Native Build ===
echo   This will open WSL and build everything from scratch.
echo   First time: ~20-30 min (toolchain build)
echo   Subsequent: ~5-10 min
echo.
pause

wsl -- bash -c "chmod +x /mnt/d/Projects/touros/toaruos-master/build-zrl.sh && bash /mnt/d/Projects/touros/toaruos-master/build-zrl.sh"
echo.
pause
