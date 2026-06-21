# Baza medycznych badań obrazowych

Aplikacja desktopowa do przechowywania, wyszukiwania i przeglądania
medycznych badań obrazowych (RTG, CT, USG, MRI). Pozwala dodawać i edytować
pacjentów oraz badania, importować pliki i całe serie DICOM, a następnie
oglądać je w aplikacji (przewijanie warstw, zoom, regulacja jasności/kontrastu).
Obsługiwane są też skompresowane obrazy DICOM (JPEG Lossless, JPEG-LS).

Stack: **C++ + Qt 6 (GUI) + PostgreSQL (baza) + DCMTK (DICOM)**.

## Szybkie uruchomienie (gotowa wersja)

Projekt jest już zbudowany. Wystarczy uruchomić:

```
start_baza.bat
```

Skrypt startuje serwer PostgreSQL (port 5433) i uruchamia aplikację
z katalogu `release\`. Przy pierwszym uruchomieniu aplikacja sama tworzy
tabele — baza startuje pusta, dane dodajesz sam.

Gotowy plik wykonywalny: `release\BazaBadanObrazowych.exe` (z ikoną).
Katalog `release\` powstaje przy budowaniu (nie ma go w repozytorium) —
jeśli go nie ma, najpierw zbuduj projekt (patrz "Budowanie od nowa").

## Pliki projektu
- `main.cpp` — cała aplikacja (baza, okno główne, dialogi, podgląd DICOM)
- `CMakeLists.txt` — konfiguracja budowania
- `app.ico` / `app.rc` — ikona pliku `.exe`
- `assets/app.png` — źródłowa grafika ikony
- `data/` — przykładowe dane DICOM do testów (`CT-00000`, `MRI-00000`)
- `start_baza.bat` — uruchamia serwer bazy + aplikację
- `release/` — gotowa wersja (exe + biblioteki DLL); generowana przy budowaniu

## Baza danych
- serwer: PostgreSQL 16 (port **5433**)
- baza: `medbaza`, użytkownik `postgres` (logowanie lokalne typu trust)
- tabele: `patients`, `studies`, `images`
- pliki DICOM trzymane są na dysku, w bazie zapisujemy tylko ścieżkę
  (`images.file_path`), powiązaną z badaniem przez `images.study_id`

Tabele tworzą się automatycznie przy starcie aplikacji (`initDB()`).

## Jak używać
1. Po lewej lista pacjentów — kliknij pacjenta, żeby zobaczyć jego badania.
2. Pole "Szukaj po nazwisku" filtruje listę pacjentów.
3. Przyciski na górze: **Dodaj pacjenta**, **Dodaj badanie**
   (dla zaznaczonego pacjenta), **Importuj DICOM** (pojedynczy plik) oraz
   **Importuj serie** (cały folder z warstwami, np. `data/MRI-00000`
   z wieloma plikami `.dcm`).
4. **Prawy przycisk myszy (PPM)** na pacjencie lub badaniu — menu
   **Edytuj / Usuń**. Usunięcie pacjenta kasuje też jego badania i obrazy,
   usunięcie badania kasuje jego obrazy.
5. Dwuklik na badanie ładuje wszystkie jego warstwy do podglądu.
6. W podglądzie DICOM:
   - **kółko myszy** lub **pionowy suwak po prawej** — przewijanie warstw
     (slice 1/267, 2/267, ...),
   - **Ctrl + kółko** — zoom,
   - suwaki **W / L** (Window / Level) — jasność i kontrast (np. tkanki
     miękkie L 40 / W 400, płuca L -500 / W 1500).

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
