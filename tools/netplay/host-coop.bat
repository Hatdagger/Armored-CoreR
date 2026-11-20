@echo off
rem Host a co-op netplay match as player 1: you and player 2 against two AI ACs (port 7777).
rem The other PC joins with join-match.bat as usual. For one AI: host-coop.bat --coop 1
rem AI style: add --ai aggressor (or balanced, sniper, duelist, evasive).
cd /d "%~dp0"
start "" armored-corer.exe --host --coop 2 %*
