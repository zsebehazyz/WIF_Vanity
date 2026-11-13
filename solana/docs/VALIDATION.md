# SolanaVanity tesztjelentés — 2026-11-13

Környezet: Debian 13 x64, GCC 14.2, CMake 4.4.4, CUDA Toolkit 13.1.115,
MinGW GCC 14-posix, Wine 10.0 és Xvfb. **A felhőben nincs NVIDIA GPU.**

## Sikeres ellenőrzések

- Linux CUDA-enabled build: **39 504 ellenőrzés sikeres**; ebből 256 a CUDA-ra
  adaptált kriptográfiai kód hoston végrehajtott összevetése. Nem GPU-s végrehajtás.
- Windows x64 EXE Wine alatt: **39 248 ellenőrzés sikeres**. A DPAPI titkosítás,
  visszafejtés és a plaintext mesterkulcs hiánya a mentésben is tesztelt.
- GUI: START, prefixhelyesség, Solana JSON-mentés, folytatás, bezárás és újranyitás
  utáni folytatás, duplikációmentesség, STOP és valódi feldolgozott batch mentése.
- solders 0.29.0 SDK: **3 RFC8032 kulcsfájl importja**, seed/publikus kulcs
  visszaállítása, aláírás és ellenőrzés; **98 külön publikus kulcs Base58-esete**.

A magtesztek tartalma:

1. Három RFC8032 Ed25519 seed/publikus kulcs tesztvektor két implementációval.
2. Python `hmac`/`hashlib` HMAC-SHA512 referenciák, 64 bites számlálóperemekkel.
3. 128 további független HMAC/Ed25519 összevetés; CUDA-adaptáció hosttesztje.
4. Minden Base58 kezdőkarakter, összetett prefixek, vezető `1` karakterek,
   nulla és speciális bájtsorok, véletlen tesztbájtok, intervallum-peremek.
   Az intervallumtesztet a tényleges Base58 karakterlánc kezdetével hasonlítjuk össze.
5. Hibás karakterek, túl hosszú prefix, hibás CPU/batch paraméterek elutasítása.
6. OS CSPRNG, JSON visszaolvasás, meglévő kulcs felülírásának megakadályozása,
   sérült kulcsfájl/állapot elutasítása, Linux 0600 jogosultság.
7. Valódi keresés, checkpoint, folytatás, tiszta futással egyező találatsorrend,
   minden exportált seed/publikus kulcs független újraellenőrzése és STOP.

Nyers kimenetek: [linux-core.txt](test-results/linux-core.txt),
[windows-core.txt](test-results/windows-core.txt),
[windows-gui.txt](test-results/windows-gui.txt), [sdk.txt](test-results/sdk.txt).
A Wine fontcache- és virtuális eszközfigyelmeztetései nem a program teszthibái;
a fájlok végén található PASS/FAIL a tesztprogram eredménye.
A Wine DPAPI szolgáltatása a kezdetben hiányos Wine-prefix miatt először hibázott;
a `wineboot --update` után változatlan titkosítási követelménnyel sikerült a teszt.

## CUDA fordítás és optimalizálás

A kernel valódi Ed25519 és HMAC/SHA512 számítást tartalmaz, nem CPU-ra delegál.
A kereső `scan_solana` és a paritásteszt `derive_solana` kernel is lefordult
`sm_86` gépi kódra, `compute_86` PTX-szel együtt.

A fordító erőforrásadatai szerint a kereső 142 regisztert használ szálanként,
720 bájt stack frame-mel, **0 spill store / 0 spill load** értékkel.
A konstans memória táblái összesen 32 440 bájtot foglalnak.
Ezek fordítási megfigyelések, nem sebesség- vagy hardveres működési bizonyítékok.

Optimalizálások: numerikus prefix-tartományok, előszámított fix bázispontos tábla,
előszámított HMAC hashállapotok, gördülő SHA512 ütemezés, batchek, pufferek
újrafelhasználása és warp-szintű találati indexgyűjtés. Nincs GPU-oldali teljes
Base58 szövegképzés minden jelöltnél.

## Benchmark

[benchmark.txt](test-results/benchmark.txt): egy CPU-szál, bemelegítés, hét kör,
2048 jelölt körönként, váltakozó futási sorrend. Ugyanazokat a seedeket vizsgáljuk
numerikus prefixszűréssel és teljes Base58-szövegképzéssel. A találatszámoknak
azonosnak kell lenniük. A mérés nem tartalmaz GUI-t, mentést vagy CPU-szálpárhuzamosítást.
Ebben a futásban a numerikus változat 27 428, a teljes Base58 változat 28 279 kulcs/s értéket adott, tehát CPU-n nem mértünk gyorsulást. A kis eltérések zajérzékenyek; ez nem Pons elleni összehasonlítás és nem GPU-benchmark.

## Még szükséges hardveres validáció

**Nem futott:** tényleges NVIDIA keresés, CPU/GPU hardveres paritás, RTX 3050
sebesség, hosszú futási stabilitás, natív Windows 10/11, MSVC build és távoli CI.
A `--require-gpu` kapcsoló hiányzó GPU esetén hibával áll meg, így a kihagyott
GPU-teszt nem kaphat látszólagos PASS eredményt.

A kicsomagolt Windows csomag könyvtárában:

```powershell
.\solana-tests.exe --require-gpu
.\solana-cli.exe device
.\solana-cli.exe benchmark
.\solana-gui-smoke.exe
```

Valódi GPU-n a teszt 256 előállított publikus kulcsot hasonlít független
CPU-eredménnyel össze, a 32 bites számlálóhatáron áthaladva is; öt külön prefixre
4096-4096 jelölt találati indexeit is teljesen összeveti CPU-val.
A GPU sebességére és Pons-szal azonos teljesítményre nincs igazolt állítás.

SDK-teszt fejlesztői környezetben:

```sh
python -m pip install solders==0.29.0
./solana/build-cpu/solana-tests --export-fixtures /tmp/solana-public-fixtures
python solana/tools/verify_sdk.py ./solana/build-cpu/solana-cli /tmp/solana-public-fixtures
```

A fixture kulcsok nyilvános RFC8032 minták, nem pénztárcák és nem használhatók pénzhez.
