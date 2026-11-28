@echo off
setlocal

echo Updating Libft from its tracked branch...
git config submodule.Libft.url git@github.com:LibftLegends/Libft.git
if errorlevel 1 exit /b %errorlevel%
git submodule sync -- Libft
if errorlevel 1 exit /b %errorlevel%
git submodule update --init --remote --merge --recursive Libft
if errorlevel 1 exit /b %errorlevel%

echo Libft submodule status:
git submodule status --recursive Libft
