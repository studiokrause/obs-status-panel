# Status Panel

Wtyczka OBS Studio, która dodaje dok **Status Panel** pokazujący dokładnie to
samo, co pasek stanu OBS (na dole okna):

- komunikaty (np. *Nagranie zapisano w …*, *Ponowne łączenie…*, *Przeciążenie kodowania!*),
- opóźnienie transmisji,
- utracone klatki (liczba i procent),
- ikonę stanu sieci (jakość połączenia) i bitrate w kbps,
- ikonę i czas transmisji,
- ikonę i czas nagrywania (z mrugającą ikoną i *(PAUSED)* podczas pauzy),
- użycie CPU,
- bieżące / docelowe FPS.

Dok można umieścić w dowolnym miejscu okna OBS, zgrupować w karty z innymi
dokami albo zostawić jako pływające okno. OBS zapamiętuje jego położenie razem z
resztą układu. Działa również wtedy, gdy oryginalny pasek stanu jest ukryty
(*Widok → Pasek stanu*).

Wymaga **OBS Studio 34 lub nowszego** (Windows x64).

## Jak to działa

- **Lustro (tryb główny)** — wtyczka znajduje oryginalny widżet paska stanu OBS
  (`StatusBarWidget`) i kopiuje tekst, ikony, aktywność i widoczność każdego
  elementu. Wszystkie wartości pochodzą więc z samego OBS i są zawsze identyczne
  z paskiem stanu — również w języku interfejsu OBS.
- **Wygląd** — panel ma tę samą strukturę i klasę co oryginał, więc motywy OBS
  (Yami, Acri, Rachni, Light, Classic…) nadają mu te same separatory i odstępy.
- **Tryb zapasowy** — jeśli przyszła wersja OBS zmieni pasek stanu tak, że nie da
  się go znaleźć, panel liczy te same wartości przez publiczne libobs /
  obs-frontend-api (ten sam algorytm co `OBSBasicStatusBar`), zamiast pokazywać
  puste pole. Teksty pochodzą wtedy z tłumaczeń OBS.
- **Układ** (prawy przycisk myszy na panelu → *Układ*): *Automatyczny* (poziomy, a
  w wąskim doku pionowy), *Poziomy* albo *Pionowy*. Ustawienie zapisuje się w
  `%APPDATA%\obs-studio\plugin_config\status-panel\settings.json`.

Języki wtyczki: `en-US`, `pl-PL`, `de-DE`, `es-ES`, `it-IT`, `fr-FR`
(`data/locale/*.ini`).

## Instalacja (Windows)

1. Pobierz `status-panel-<wersja>-windows-x64.zip` z zakładki *Releases*.
2. Rozpakuj do `C:\ProgramData\obs-studio\plugins\` tak, aby powstało:

   ```
   C:\ProgramData\obs-studio\plugins\status-panel\bin\64bit\status-panel.dll
   C:\ProgramData\obs-studio\plugins\status-panel\data\locale\*.ini
   ```

3. Uruchom OBS i włącz dok w menu *Doki → Panel stanu* (*Status Panel*).

## Budowanie

Potrzebne: CMake 3.22+, Visual Studio (MSVC), Qt 6 oraz pakiety CMake `libobs` i
`obs-frontend-api` (np. z [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
/ zbudowanego OBS i `obs-deps`).

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="C:/path/to/obs-sdk;C:/path/to/obs-deps-qt6;C:/path/to/obs-deps"
```

```bash
cmake --build build --config RelWithDebInfo
```

```bash
cmake --install build --config RelWithDebInfo --prefix dist
```

Release 1.0 zbudowano z nagłówkami OBS 31.1.0 i Qt 6.8 (`obs-deps` 2025-05-23).
libobs, obs-frontend-api i Qt 6 zachowują zgodność binarną wstecz, więc wtyczka
działa w OBS 34 i nowszych.

## Wersjonowanie

Wersje numerowane od `1.0` co `0.1` (`1.0`, `1.1`, `1.2`, …). Numer jest w
`CMakeLists.txt` (`project(... VERSION ...)`), tag release to `v<wersja>`.

## Licencja

GPL-2.0-or-later (jak OBS Studio).
