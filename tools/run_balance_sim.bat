@echo off
rem Edit assets\data\circular\*.csv, then run this: builds the simulator and prints/writes the balance
rem timeline. Args pass through: run_balance_sim.bat --seconds 300 --interval 5 --out build\tools\try1
rem See docs/circular-balance.md.
call "%~dp0build_balance_sim.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
build\tools\balance_sim.exe %*
