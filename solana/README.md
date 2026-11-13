# SolanaVanity — CUDA prefix generátor

[Windows x64 ZIP letöltése](downloads/SolanaVanity-Windows-x64.zip) ·
[Önálló EXE](downloads/SolanaVanity.exe) · [Teszteredmények](docs/VALIDATION.md)

Natív C++ / CUDA C++ program, egyszerű Windows GUI-val, **Solana-cím elején álló
prefix keresésére**. RTX 3050 Laptop GPU célhardverre fordított `sm_86` kernel és
`compute_86` PTX kerül az EXE-be. GPU nélkül ugyanaz a program CPU-n keres.

A ZIP kicsomagolása után indítsd a **`SolanaVanity.exe`** fájlt. Nem kell Python,
CUDA Toolkit, Solana CLI vagy külön C++ runtime telepítés a futtatáshoz. GPU módhoz
**CUDA 13.1-et támogató NVIDIA driver** kell; a program a driver `nvcuda.dll` fájlját
tölti be. Más architektúrához újrafordítható. **A GPU-kód lefordult, de a fejlesztői
felhőben nincs NVIDIA GPU: a valódi RTX 3050 futtatás és sebesség még ellenőrizendő.**
A kiadás tesztkiadás, nem független biztonsági audit eredménye.

## Használat

1. Prefix: például `Ab`, `pump`, `A7`, `SoL`. Kis-/nagybetűérzékeny.
   A Base58-ban nincs `0`, nagy `O`, nagy `I` vagy kis `l`; a `Solana` szöveg
   ezért változatlanul nem használható prefixként. Nincs automatikus átírás.
2. Találati korlát: 1–10 000. Alapból 1, mert jellemzően egy saját cím kell.
3. CPU szálak: 0 = automatikus (legfeljebb 8), kézzel 1–32.
4. GPU batch: alapból 4096; 256–65 536 között állítható, 128-cal osztható értékre.
   Ez konzervatív kezdőérték a laptophoz, nem benchmarkkal igazolt optimum.
   Saját GPU-n érdemes 8192/16384 értékekkel összehasonlítani. A nagyobb batch
   növelheti a STOP késleltetését. CPU fallbackben legfeljebb 1024 jelölt jut egy batchre.
5. START új, operációsrendszer-eredetű véletlen mesterkulcsot készít. STOP az aktuális
   batchet befejezi, elmenti az állapotot, és megáll. FOLYTATÁS ugyanonnan folytat,
   bezárás/újranyitás után is. A találati korlát elérése után a FOLYTATÁS egy további
   találatig keres; nagyobb értéket is beírhatsz a korlát mezőbe.
6. Jelölj ki egy címet a másoláshoz. A „Kulcsfájlok” gomb megnyitja a mentési helyet.
   A „Lista törlése” a listát törli; **a már létrehozott kulcsfájlokat megőrzi**.

A GUI külön szálon fut, 250 ms-onként frissít. Virtuális listanézetet használ.
A sebesség ténylegesen megvizsgált seed/publikuskulcs-jelölt másodpercenként.
A kijelzett átlagos találati idő valószínűségi becslés, **nem visszaszámlálás**:
a keresés memóriamentes valószínűségi jellegű, egy hosszú prefix sok időbe kerülhet.
A 43/44 karakteres címek eltérő gyakoriságát és a vezető `1` karaktereket figyelembe
vevő intervallumszélességből becslünk, közel egyenletes publikus bájtoszlást feltételezve.

## Mentés és Solana-kompatibilitás

Alapkönyvtár: `%LOCALAPPDATA%\SolanaVanity`.

- `keys/<teljes-cím>.json`: szabványos, **64 decimális bájtos JSON tömb**,
  `32 bájt seed || 32 bájt publikus kulcs`. Nem a SHA512-es kibontott skalár kerül ide.
- `results.csv`: publikus cím, kulcsfájl neve, találati idő UTC-ben. Nincs benne privát kulcs.
- `search.state`: prefix, beállítások, számláló, eredmények és a folytatáshoz szükséges mesterkulcs.
  Windowson a mesterkulcsot a felhasználói fiókhoz/géphez kötött **DPAPI titkosítja**.
- `session.lock`: ugyanazt a könyvtárat egyszerre egy GUI használhatja.

A `keys/*.json` fájlok **titkosítatlan privát kulcsok**. A pénztárcába importáláshoz
hordozhatók, de titokban kell tartani és biztonságosan menteni őket.
A DPAPI-s állapotfájl önmagában nem hordozható más Windows-fiókba; más gépre a JSON
kulcsfájlokat kell menteni, nem csak az állapotot. Egyedi hely:

```powershell
SolanaVanity.exe --data-dir D:\PrivateWallets\SolanaVanity
```

Linuxon a folytatási állapot titkosítatlan, 0600 jogosultságú fájl; a létrehozott
könyvtárak 0700, a kulcsfájlok 0600 jogosultságúak. A Windows JSON-fájlok a választott
könyvtár hozzáféréseit öröklik, ezért privát könyvtárat használj. A program nem
küld kulcsokat hálózatra, nem igényel RPC-t és nem csatlakozik tárcához.

Az alkalmazás minden találatnál a **Monocypher független Ed25519 és HMAC**
implementációjával ellenőriz. A `solders` 0.29.0 Solana SDK-val külön teszteltük a
JSON-importot, a seedből visszaállítást és az aláírások ellenőrzését.
Telepített Solana CLI-vel a szokásos használat például:

```powershell
solana-keygen pubkey .\keys\<teljes-cim>.json
```

A Solana CLI parancsot a felhőben nem futtattuk; az SDK-interoperabilitás viszont tesztelt.

## Mitől prefixre optimalizált?

- **Valódi GPU**: HMAC-SHA512 seed-származtatás, SHA512, Ed25519 fix bázispontos
  skalárszorzás, publikus kulcs csomagolása és prefixellenőrzés a CUDA-kernelben.
- A prefixet előre pontos, 256 bites **numerikus intervallumokra** alakítjuk.
  A hot loopban nincs teljes Base58 szövegképzés; ez csak találatkor történik.
- Körülbelül 32 KB előszámított Ed25519 tábla CUDA konstans memóriában.
- Előszámított HMAC inner/outer hashállapot; a SHA512 CUDA-változata 16 szavas
  gördülő ütemezést használ 80 szavas szálankénti tömb helyett.
- Újrafelhasznált device pufferek, batchelt végrehajtás, warp-szintű találatgyűjtés;
  a kereső csak találati indexeket hoz vissza, minden cím helyett.

A Pons VanitySearch Bitcoin secp256k1 címkereső: annak görbearitmetikája nem másolható
át Solanához. Ez önálló Ed25519 backend, hasonló batchelt GPU-munkaszervezéssel.
Nem tartalmaz Pons-forráskódot, és nem állít azzal azonos sebességet.
A mért CPU összehasonlítás [itt](docs/test-results/benchmark.txt) olvasható;
GPU sebességnövekedést mérés nélkül nem állítunk.

A seedek `HMAC-SHA512(master, "SolanaVanity-v1" || counter_le64)[0:32]` értékek.
A 256 bites mesterkulcs a Windows BCryptGenRandom/Linux getrandom CSPRNG-ből jön.
Nem időbélyeg, nem `rand()`, nem ismert seed +1 léptetése, és nincs felhasználói
jelszóból tippelhető kulcstér. A publikus pont léptetésével előállítható skalárok
helyett végig szabványos, visszaállítható Solana seedeket keresünk.

## Fordítás Windowsra

Visual Studio 2022 C++ x64, CMake >=3.24, CUDA Toolkit 13.1 és kompatibilis MSVC.
A CUDA-adaptáció generálásához Python 3 kell **a fejlesztői gépen**, a futtatáshoz nem.
A tárhely gyökeréből, x64 Native Tools terminálban:

```powershell
cmake -S solana -B solana/build-msvc -A x64 -DSOLVAN_CUDA=ON -DSOLVAN_ARCH=86
cmake --build solana/build-msvc --config Release --parallel 4
ctest --test-dir solana/build-msvc -C Release --output-on-failure
.\solana\build-msvc\Release\solana-gui-smoke.exe
.\solana\build-msvc\Release\solana-tests.exe --require-gpu
.\solana\build-msvc\Release\solana-cli.exe benchmark
```

CPU-only: `-DSOLVAN_CUDA=OFF`. C/C++ runtime statikusan linkelt; a program Windows
rendszer-DLL-eket használ. A CUDA fatbin az EXE-be épül, nincs külön CUDA runtime DLL.

## Linux és keresztfordítás a jelenlegi felhőben

```sh
# A tárhely gyökeréből; root nélkül telepíti a már dokumentált toolchain-t:
python tools/bootstrap_cloud.py
CMAKE=/workspace/.tools/python/cmake/data/bin/cmake
"$CMAKE" -S solana -B solana/build-cuda -DCMAKE_BUILD_TYPE=Release \
  -DSOLVAN_CUDA=ON -DCMAKE_CUDA_COMPILER=/workspace/.tools/cuda13/bin/nvcc
"$CMAKE" --build solana/build-cuda --parallel 4
/workspace/.tools/python/cmake/data/bin/ctest --test-dir solana/build-cuda --output-on-failure
"$CMAKE" -S solana -B solana/build-win -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/mingw-cloud.cmake" \
  -DSOLVAN_CUDA=ON -DSOLVAN_FATBIN="$PWD/solana/build-cuda/solana.fatbin"
"$CMAKE" --build solana/build-win --parallel 4
```

CPU-only Linux fejlesztéshez C/C++17 fordító és CMake elég:

```sh
cmake -S solana -B solana/build-cpu -DCMAKE_BUILD_TYPE=Release
cmake --build solana/build-cpu --parallel 4
ctest --test-dir solana/build-cpu --output-on-failure
./solana/build-cpu/solana-cli search Ab --cpu --limit 1 --out /private/solana-test
./solana/build-cpu/solana-cli resume /private/solana-test --limit 2
./solana/build-cpu/solana-cli verify /private/solana-test/keys/<cim>.json
```

CLI futásokhoz mindig külön kimeneti könyvtárat használj; ugyanazt egyszerre ne írd
másik CLI-ből/GUI-ból. A JSON-fájlokat a program nem írja felül eltérő kulccsal.
Referenciák, licencek: [THIRD_PARTY.md](docs/THIRD_PARTY.md).
