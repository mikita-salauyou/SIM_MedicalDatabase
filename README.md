# Baza medycznych badań obrazowych

Aplikacja desktopowa do przechowywania, wyszukiwania i przeglądania
medycznych badań obrazowych (RTG, CT, USG, MRI). Pozwala dodawać i edytować
pacjentów oraz badania, importować pojedyncze pliki i całe serie DICOM,
a potem oglądać je w aplikacji (przewijanie warstw, zoom, regulacja
jasności i kontrastu). Obsługiwane są też skompresowane obrazy DICOM
(JPEG Lossless, JPEG-LS).

Pliki DICOM leżą na dysku, a w bazie trzymane są tylko metadane i ścieżki.
Listę obrazów można dodatkowo wyeksportować do pliku XML.

Stack: **C++ + Qt 6 (GUI) + PostgreSQL (przez ODBC) + DCMTK (DICOM)**.

## Szybkie uruchomienie

Projekt jest już zbudowany. Wystarczy uruchomić:

```
start_baza.bat
```

Skrypt startuje serwer PostgreSQL (port 5433) i uruchamia aplikację
z katalogu `release\`. Po starcie pojawia się okno logowania (domyślnie
serwer `127.0.0.1`, port `5433`, baza `medbaza`, użytkownik `postgres`).
Przy pierwszym uruchomieniu aplikacja sama tworzy tabele — baza startuje
pusta, dane dodajesz sam.

Gotowy plik wykonywalny: `release\BazaBadanObrazowych.exe` (z ikoną).
Katalog `release\` powstaje przy budowaniu (nie ma go w repozytorium) —
jeśli go nie ma, najpierw zbuduj projekt (patrz "Budowanie od nowa").

> Potrzebny jest sterownik **psqlODBC** (PostgreSQL Unicode x64)
> zainstalowany w systemie — bez niego połączenie się nie powiedzie.

## Pliki projektu
- `main.cpp` — cała aplikacja (baza, okno logowania, okno główne, podgląd DICOM)
- `CMakeLists.txt` — konfiguracja budowania
- `app.ico` / `app.rc` — ikona pliku `.exe`
- `assets/app.png` — źródłowa grafika ikony
- `data/` — przykładowe dane DICOM do testów (`CT-00000`, `MRI-00000`)
- `start_baza.bat` — uruchamia serwer bazy + aplikację
- `release/` — gotowa wersja (exe + biblioteki DLL); generowana przy budowaniu

## Baza danych
- serwer: PostgreSQL 16 (port **5433**), baza `medbaza`, użytkownik `postgres`
- połączenie przez ODBC (sterownik *PostgreSQL Unicode(x64)*)
- tabele:
  - `patients` — pacjenci
  - `studies` — badania (powiązane z pacjentem)
  - `series` — serie w badaniu (modality, opis)
  - `images` — obrazy (`series_id`, `source_id`, `file_path` względny do źródła)
  - `sources` — katalogi z plikami (dodawane automatycznie przy imporcie)
- pliki DICOM trzymane są na dysku; w bazie zapisujemy ścieżkę względną
  do katalogu źródła, pełna ścieżka = `sources.sciezka + images.file_path`

Tabele tworzą się automatycznie przy starcie aplikacji. Przy imporcie
z nowego katalogu źródło samo dopisuje się do tabeli `sources`.

## Jak używać
1. Po lewej lista pacjentów — kliknij pacjenta, żeby zobaczyć jego badania.
2. Pole "Szukaj po nazwisku" filtruje listę pacjentów.
3. Przyciski na górze: **Dodaj pacjenta**, **Dodaj badanie**
   (dla zaznaczonego pacjenta), **Importuj DICOM** (pojedynczy plik),
   **Importuj serie** (cały folder z warstwami, np. `data/MRI-00000`)
   oraz **Eksportuj XML** (zapis listy obrazów do pliku: ścieżka, rozmiar, data).
4. **Prawy przycisk myszy (PPM)** na pacjencie lub badaniu — menu
   **Edytuj / Usuń**. Usunięcie pacjenta kasuje też jego badania i obrazy,
   usunięcie badania kasuje jego obrazy.
5. Dwuklik na badanie ładuje wszystkie jego warstwy do podglądu.
6. W podglądzie DICOM:
   - **kółko myszy** lub **pionowy suwak po prawej** — przewijanie warstw,
   - **Ctrl + kółko** — zoom,
   - suwaki **W / L** (Window / Level) — jasność i kontrast (np. tkanki
     miękkie L 40 / W 400, płuca L -500 / W 1500).

> Seria DICOM to folder z wieloma plikami `.dcm` (po jednym na warstwę).
> Importuj cały folder przez **Importuj serie**, a potem przewijaj warstwy
> kółkiem myszy.

## Środowisko (gdzie co jest zainstalowane)
- Qt 6.8.1 (MinGW): `C:\Qt\6.8.1\mingw_64`
- Kompilator MinGW 13.1.0: `C:\Qt\Tools\mingw1310_64`
- DCMTK 3.6.8 (zbudowane statycznie, z modułami JPEG): `C:\dcmtk`
- PostgreSQL 16 (binaria): `C:\pg\pgsql`, katalog danych: `C:\pgdata`
- sterownik ODBC: psqlODBC (PostgreSQL Unicode x64) zainstalowany w systemie

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

Po zbudowaniu zebranie bibliotek obok exe (windeployqt dołącza też
sterownik `sqldrivers\qsqlodbc.dll`):
```powershell
C:\Qt\6.8.1\mingw_64\bin\windeployqt.exe --release release\BazaBadanObrazowych.exe
```
Sterownik psqlODBC instaluje się osobno (z paczki MSI z postgresql.org),
wymaga uprawnień administratora.

## Ręczne uruchomienie serwera bazy
```powershell
C:\pg\pgsql\bin\pg_ctl.exe -D C:\pgdata -o "-p 5433" start
C:\pg\pgsql\bin\psql.exe -h 127.0.0.1 -p 5433 -U postgres -d medbaza
```
