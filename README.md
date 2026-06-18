# Baza medycznych badań obrazowych

Aplikacja desktopowa do przechowywania, wyszukiwania i przeglądania
medycznych badań obrazowych (RTG, CT, USG, MRI). Pozwala dodawać pacjentów
i badania, importować pliki DICOM i oglądać je w aplikacji.

Stack: **C++ + Qt 6 (GUI) + PostgreSQL (baza) + DCMTK (DICOM)**.

## Szybkie uruchomienie (gotowa wersja)

Projekt jest już zbudowany. Wystarczy uruchomić:

```
start_baza.bat
```

Skrypt startuje serwer PostgreSQL (port 5433) i uruchamia aplikację
z katalogu `release\`. Przy pierwszym uruchomieniu aplikacja sama tworzy
tabele i wstawia dane testowe (2 pacjentów + 3 badania).

Gotowy plik wykonywalny: `release\BazaBadanObrazowych.exe` (z ikoną).

## Pliki projektu
- `main.cpp` — cała aplikacja (baza, okno główne, dialogi, podgląd DICOM)
- `CMakeLists.txt` — konfiguracja budowania
- `app.ico` / `app.rc` — ikona pliku `.exe`
- `assets/app.png` — źródłowa grafika ikony
- `start_baza.bat` — uruchamia serwer bazy + aplikację
- `release/` — gotowa, samodzielna wersja (exe + wszystkie biblioteki DLL)

## Baza danych
- serwer: PostgreSQL 16 (port **5433**)
- baza: `medbaza`, użytkownik `postgres` (logowanie lokalne typu trust)
- tabele: `patients`, `studies`, `series`, `images`
- pliki DICOM trzymane są na dysku, w bazie zapisujemy tylko ścieżkę

Tabele tworzą się automatycznie przy starcie aplikacji (`initDB()`).

## Jak używać
1. Po lewej lista pacjentów — kliknij pacjenta, żeby zobaczyć jego badania.
2. Pole "Szukaj po nazwisku" filtruje listę pacjentów.
3. Przyciski na górze: **Dodaj pacjenta**, **Dodaj badanie**
   (dla zaznaczonego pacjenta), **Importuj DICOM** (pojedynczy plik) oraz
   **Importuj serie** (cały folder z warstwami, np. `series-00001` z setkami
   plików `.dcm`).
4. **Prawy przycisk myszy (PPM)** na pacjencie lub badaniu — menu
   **Edytuj / Usuń**. Usunięcie pacjenta kasuje też jego badania i obrazy,
   usunięcie badania kasuje jego serie i obrazy.
5. Dwuklik na badanie ładuje wszystkie jego warstwy do podglądu.
6. W podglądzie DICOM:
   - **kółko myszy** — przewijanie warstw (slice 1/267, 2/267, ...),
   - **Ctrl + kółko** — zoom,
   - suwaki **Okno / Poziom** — jasność i kontrast (np. tkanki miękkie
     Poziom 40 / Okno 400, płuca Poziom -500 / Okno 1500).

> Seria DICOM to folder z wieloma plikami `.dcm` (po jednym na warstwę).
> Importuj cały folder przez **Importuj serie**, a potem przewijaj warstwy
> kółkiem myszy.

## Środowisko (gdzie co jest zainstalowane)
- Qt 6.8.1 (MinGW): `C:\Qt\6.8.1\mingw_64`
- Kompilator MinGW 13.1.0: `C:\Qt\Tools\mingw1310_64`
- DCMTK 3.6.8 (zbudowane statycznie): `C:\dcmtk`
- PostgreSQL 16 (binaria): `C:\pg\pgsql`, katalog danych: `C:\pgdata`

## Budowanie od nowa
```powershell
$env:Path = "C:\Qt\Tools\mingw1310_64\bin;C:\Users\nikki\AppData\Roaming\Python\Python314\Scripts;" + $env:Path
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.1\mingw_64" -DDCMTK_DIR="C:\dcmtk\cmake"
cmake --build build
```
> Uwaga: budowanie w ścieżce ze spacją (`maga II sem`) psuje `windres`
> (ikonę). Dlatego gotowy build powstał w `C:\sim`, a wynik skopiowano
> do `release\`. Do samej kompilacji `.exe` (bez ikony) ścieżka ze spacją
> nie przeszkadza.

Po zbudowaniu, żeby zebrać biblioteki obok exe:
```powershell
C:\Qt\6.8.1\mingw_64\bin\windeployqt.exe --release release\BazaBadanObrazowych.exe
Copy-Item C:\pg\pgsql\bin\libpq.dll release\
```

## Ręczne uruchomienie serwera bazy
```powershell
C:\pg\pgsql\bin\pg_ctl.exe -D C:\pgdata -o "-p 5433" start
C:\pg\pgsql\bin\psql.exe -h 127.0.0.1 -p 5433 -U postgres -d medbaza
```
