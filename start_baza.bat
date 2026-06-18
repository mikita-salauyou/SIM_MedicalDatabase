@echo off
chcp 65001 >nul
echo === Baza medycznych badan obrazowych ===
echo.
echo [1/2] Uruchamiam serwer PostgreSQL na porcie 5433...
"C:\pg\pgsql\bin\pg_ctl.exe" -D "C:\pgdata" -l "C:\pgdata\log.txt" -o "-p 5433" start
echo.
echo [2/2] Uruchamiam aplikacje...
start "" "%~dp0release\BazaBadanObrazowych.exe"
echo.
echo Gotowe. Jesli aplikacja sie nie polaczyla, poczekaj chwile az serwer wstanie i uruchom ponownie.
