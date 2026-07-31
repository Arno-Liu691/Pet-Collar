@echo off
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude -Ialgorithm src\main_pc.c src\csv_reader.c algorithm\rest_detector.c algorithm\vital_estimator.c -o pet_collar_pc_v18.exe -lm
if errorlevel 1 exit /b 1
echo Build successful: pet_collar_pc_v18.exe
