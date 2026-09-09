@echo off
title SIH 2026 MINE RESCUE ROVER - ALL NODES SIMULATOR
color 0A
echo =====================================================================
echo   SIH 2026 MINE RESCUE ROVER - 8-NODE CONCURRENT MESH SIMULATION
echo =====================================================================
echo Starting all 8 nodes (Rover, R1A, R1B, R2, R3, R4A, R4B, Gateway)...
echo.
python run_all_nodes.py
pause
