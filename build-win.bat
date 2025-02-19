@echo off
echo Preparing to build Personal Audio Mixer...

echo Cleaning old build files...
if exist dist rmdir /s /q dist
if exist build\Release\*.node mkdir resources 2>nul

echo Rebuilding native modules...
npm run rebuild

echo Packaging application...
npm run build:win

echo Build complete! Check the 'dist' folder for your installable and portable .exe files.
pause