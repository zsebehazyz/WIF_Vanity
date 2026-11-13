# VanityWIF — CUDA WIF mintakereső

**Solana prefix generátor:** [SolanaVanity külön projekt és Windows EXE](solana/README.md).

**Letöltés:** [Windows x64 ZIP (CUDA + CPU)](downloads/VanityWIF-Windows-x64.zip) ·
[VanityWIF.exe](downloads/VanityWIF.exe) · [Teszteredmények](docs/VALIDATION.md)

A ZIP-et csomagold ki, és indítsd a `VanityWIF.exe` fájlt. Tesztkiadás: a GPU-kód
lefordult, de valódi RTX 3050-en még ellenőrizni kell.

C++17 / CUDA C++ alkalmazás Windows 10/11 x64 rendszerre, natív grafikus felülettel,
CPU fallbackkel, matematikailag célzott Base58Check mintakereséssel. A kiadási
csomag `VanityWIF.exe` fájlja **beágyazott sm_86 CUDA-kernelt és PTX-et** tartalmaz;
az NVIDIA RTX 3050 Laptop GPU támogatott. Python és CUDA Toolkit nem kell a futtatáshoz.
Friss, **CUDA 13.1-et támogató NVIDIA driver** szükséges a GPU módhoz. Driver vagy
megfelelő GPU nélkül ugyanaz az EXE CPU-n működik. A GPU hardveres futtatása ebben
a GPU nélküli fejlesztői környezetben nem volt tesztelhető.

**Kutatási eszköz. A kötött mintájú privát kulcsok determinisztikusan előállíthatók,
nyilvánosan újrakereshetők; pénz tárolására nem biztonságosak.** A WIF, a HEX,
a vágólap és a mentések titkosítatlan privát kulcsot tartalmaznak.

## Használat

1. Indítsd el a `VanityWIF.exe` fájlt. A GPU felismerése induláskor történik.
2. Adj meg egy változatlanul megőrzendő Base58 kifejezést. Tiltott: `0`, `O`, `I`, `l`,
   ékezetek, szóközök. A `HelloWorldThisIsAttila` ezért nem érvényes kifejezés;
   a program nem módosítja csendben a szöveget.
3. A kitöltők sorrendje alapból `K,L,J,M,H,G,S,A`, majd a többi Base58 karakter.
   A minimum/maximum kitöltés az összes ismétlődő kitöltőkarakter számát jelenti.
   Az elválasztók alapból `1` és `1`; üresre is állíthatók.
4. Válaszd ki a compressed/uncompressed formátumot és a végződéstartományt.
   Alapból 4–6; nagyobb érték csak a maximum kifejezett növelésével, legfeljebb 12.
   A több kitöltőt tartalmazó minták elsőbbséget kapnak, utána a rövidebb végződés,
   végül az általad megadott kitöltősorrend dönt.
5. START indít, STOP befejezi az aktuális, legfeljebb 65 536 jelöltes GPU-batchet,
   és ment. Nincs futó kernel erőszakos megszakítása vagy CUDA context reset.
   FOLYTATÁS a következő fel nem dolgozott jelölttől folytat, újraindítás után is.
   A találati korlát elérésekor megáll; a GUI FOLYTATÁS ilyenkor 100-zal emeli a korlátot
   (maximum 100 000). Az új START megerősítés után lecseréli az előző keresést.
6. Jelölj ki egy találatot a WIF/HEX másolásához. A „Mentések” gomb megnyitja a fájlokat.
   A törlés a találatokat törli; a keresési pozíciót megőrzi.

Mentés: `%LOCALAPPDATA%\VanityWIF\search.state`, `results.csv`, `results.txt`.
A paraméterek, az eltelt idő, a találatok és az aktuális mintasorszám + kulcsoffset
kétmásodpercenként és szabályos megálláskor mentődnek. A már bejárt mintákat a
verziózott, determinisztikus keresési terv sorszáma írja le. A fájlok cseréje atomi;
váratlan leálláskor legfeljebb az utolsó mentés óta végzett munka ismétlődik.
Egyszerre egy GUI-példány használhat egy mentési könyvtárat.
Egyedi hely: `VanityWIF.exe --data-dir D:\WIFResearch`.
A CLI a jelenlegi könyvtárba ment; külön futásokhoz használj külön könyvtárat.

A sebesség rövid időablakban mért **kulcsjelölt/s**, nem végződés-karakterlánc/s.
Az ETA a teljes terv bejárására vonatkozik, a találati korlát miatt korábbi megállás
lehetséges. GPU-kihasználtság az NVIDIA NVML-ből érkezik, ha elérhető; különben `n/a`.
A GPU mód minden jelölt checksumját és intervallum-összehasonlítását a GPU-n végzi.
A rövid, néhány kulcsos intervallumoknál a CUDA indítási költsége dominálhat;
ilyenkor a CPU mód praktikusabb lehet. Nem állítunk mért GPU-gyorsulást.

## A megadott ismert WIF

```
5K1MysteryBansheePromotion1KKKKKKKKKKKKKKKKKKKuLH7t
```

Érvényes, mainnet, uncompressed, 51 karakter / 37 dekódolt bájt.
A hozzá tartozó HEX:

```
9cd85e9576499f4cbd0e5012f3ce964dddc039e8a2b2c9c184119d83ff630e60
```

A keresési terv tartalmazza ezt a mintát és ezt a kulcsot. Minden találatot külön
Base58 dekóder és a kereső SHA-kódjától független SHA256 szolgáltató ellenőriz:
Windowson Windows CNG (`bcrypt.dll`), Linuxon OpenSSL. Ellenőrzött: mainnet `0x80`,
32 bájtos kulcs, `1 <= key < secp256k1_n`, compressed esetben `0x01`, double SHA256.
A matematikai leírás: [docs/ALGORITHM.md](docs/ALGORITHM.md).

## Windows fordítás

Szükséges: Visual Studio 2022 C++ x64 eszközök, CMake >=3.24; GPU-s fordításhoz
CUDA Toolkit 13.1 és annak támogatott MSVC verziója. „x64 Native Tools” terminálban:

```powershell
cmake -S . -B build-win -A x64 -DVANITY_CUDA=ON -DVANITY_CUDA_ARCH=86
cmake --build build-win --config Release --parallel 4
ctest --test-dir build-win -C Release --output-on-failure
.\build-win\Release\vanity-tests.exe --require-gpu
.\build-win\Release\vanity-cli.exe benchmark
.\build-win\Release\VanityWIF.exe
```

CPU-only kiadás: `-DVANITY_CUDA=OFF`. A C++ futtatókörnyezet statikusan linkelt.
Nincs Qt, Python vagy külön CUDA runtime DLL. A GUI Windows rendszer-DLL-eket használ;
GPU módban dinamikusan tölti be a driver `nvcuda.dll` fájlját, opcionálisan `nvml.dll`-t.
A beépített sm_86 gépi kód az RTX 3050-re készült; a PTX újabb architektúrákon a
driver JIT fordítóját használhatja. A kézi `VANITY_CUDA_ARCH` értékhez illeszkedő
fatbin szükséges; egy már meglévő fatbin architektúráját nem változtatja meg a kapcsoló.

## Linux / felhőkörnyezet

Natív CPU fejlesztéshez: GCC/Clang C++17, CMake, OpenSSL fejlesztői csomag.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/vanity-cli verify 5K1MysteryBansheePromotion1KKKKKKKKKKKKKKKKKKKuLH7t
./build/vanity-cli search MysteryBansheePromotion --fillers K --fixed-position --limit 10
./build/vanity-cli resume
```

A jelenlegi Debian 13 alapú felhőben root nélkül reprodukálható az eszközkészlet:

```sh
python tools/bootstrap_cloud.py
CMAKE=/workspace/.tools/python/cmake/data/bin/cmake
"$CMAKE" -S . -B build-cuda13 -DCMAKE_BUILD_TYPE=Release -DVANITY_CUDA=ON \
  -DCMAKE_CUDA_COMPILER=/workspace/.tools/cuda13/bin/nvcc
"$CMAKE" --build build-cuda13 --parallel 4
/workspace/.tools/python/cmake/data/bin/ctest --test-dir build-cuda13 --output-on-failure
"$CMAKE" -S . -B build-win-cuda -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-cloud.cmake -DVANITY_CUDA=ON \
  -DVANITY_CUDA_FATBIN="$PWD/build-cuda13/vanity.fatbin"
"$CMAKE" --build build-win-cuda --parallel 4
```

A Linuxon fordított CUDA fatbin operációsrendszer-független GPU-kód, ezért a Windows
EXE-be is beágyazható. A Linux CLI és a Windows GUI ugyanazt a CUDA Driver API
backendet használja. A keresztfordított EXE: `build-win-cuda/VanityWIF.exe`.
A bootstrap csak `/workspace/.tools` alatt telepít, nem változtat rendszerfájlokat.
CUDA 12.9 a jelenlegi glibc 2.41 fejléceivel inkompatibilis volt; ezért 13.1-et használunk.

Tesztelés: [docs/VALIDATION.md](docs/VALIDATION.md). Külső komponensek:
[docs/THIRD_PARTY.md](docs/THIRD_PARTY.md). A GitHub Actions CPU CI Linux és Windows
rendszeren fordít és tesztel; a GPU-teszt valódi NVIDIA hardvert igényel.
