# Tesztjelentés — 2026-11-13

## Ellenőrzött környezet

- Debian 13 x86_64, GCC 14.2, CMake 4.4.4, OpenSSL 3.5.7.
- NVIDIA CUDA Toolkit 13.1.115; `sm_86` gépi kód + `compute_86` PTX.
- Windows x64 keresztfordítás: MinGW GCC 14-posix, Release, statikus C++ runtime.
- Windows futási teszt: Wine 10.0, Xvfb virtuális képernyő. Ez nem natív Windows-hardveres teszt.
- **Nincs NVIDIA GPU a felhőben. GPU-n történő keresés, CPU/GPU paritás és GPU benchmark nem futott.**

## Magtesztek

Linuxon és a CUDA-kernelt tartalmazó Windows EXE tesztprogramján:

```
CUDA tests SKIPPED: CPU fallback: NVIDIA driver not installed
PASS 554 assertions
```

A 554 ellenőrzés tartalma:

- A felhasználó ismert WIF-je, elvárt HEX kulcsa és pontos visszakódolása.
- Privát kulcs = 1 ismert compressed és uncompressed WIF-vektor.
- Hibás checksum; rövid/hosszú WIF; tiltott Base58 karakter.
- Helyes checksum mellett nulla, secp256k1 rendjével egyenlő, illetve túl nagy kulcs.
- Hibás compressed jelölő; testnet verzió elutasítása.
- Többbájtos kulcsszámláló átvitel.
- 512 SHA256/Base58Check körteszt független OpenSSL/CNG ellenőrzéssel.
- A megadott mintakulcs intervallumba esése és a tervben szereplése.
- Compressed intervallumteszt összevetése a valódi Base58 prefixszel.
- A kifejezés átfedése a kötelező WIF-fejléccel.
- Keresés, állapotmentés, beolvasás, találati korláttól folytatás és tiszta futással egyező eredmények.
- CSV/TXT export; sérült állapotfájl elutasítása.
- Aktív keresés STOP-ja, mentett valódi előrehaladás, folytatás és duplikációmentesség.

A GPU-t kötelezővé tevő teszt itt szándékosan hibával zárul:

```
vanity-tests --require-gpu
FAIL after 555: A real compatible NVIDIA GPU is required for this acceptance run
```

Így a GPU hiánya nem téveszthető össze sikeres hardveres validációval.
Valódi eszközön a teszt az összes GPU-találati indexet CPU-val hasonlítja össze,
compressed és uncompressed intervallumokban, legfeljebb 65 536 jelöltes batchekkel.

## Funkcionális GUI ellenőrzés

Kézzel futtatva: START után 100 érvényes találat, CSV 101 sor fejléccel;
FOLYTATÁS után 200 találat. Minden találat átment a külön CPU validátoron.
Például az első mentett találat:

```
5K6661MysteryBansheePromotion166666666666666666n9s5
a79378df9bf0713d4ea0d5511a2a24cbb7ed13c641e8760b4919c97fd3ab43f7
```

Az automatizált GUI smoke teszt a valódi EXE-t indítja elkülönített könyvtárral.
START, FOLYTATÁS, bezárás/újranyitás utáni folytatás, mentett WIF-ek ellenőrzése
és hosszabb keresés STOP-ja szerepel benne. A végleges futás nyers eredménye:
[test-results/windows-gui.txt](test-results/windows-gui.txt).

A teszt fejlesztésekor kiderült: rövid, kizárólag K-val kitöltött véges tervből
nem várható tetszőleges számú találat; a teszt végződéstartományát ezért bővítettük.
A UI nagy találatszámú blokkolását virtuális listanézet javította. A smoke teszt
csak a látható, kész ablakon állítja be a mezőket, elkerülve az indulási versenyhelyzetet.

Újrafuttatás Windows desktopon, a két EXE egy könyvtárban:

```powershell
.\vanity-tests.exe
.\vanity-gui-smoke.exe
.\vanity-tests.exe --require-gpu
.\vanity-cli.exe benchmark
```

A teszt alapból nem igényel GUI-t; `-DVANITY_GUI_TESTS=ON` regisztrálja a desktopot
igénylő smoke tesztet a CTestbe is.

## Teljesítmény és bináris

CPU benchmark: 65 536 intervallumkulcsból 65 535 egyezés; a mért idő/átviteli sebesség
[test-results/benchmark.txt](test-results/benchmark.txt) alatt található.
A kezdeti mérések kb. 0,042 másodpercet, 1,5–1,6 millió kulcsjelölt/s értéket adtak.
Ez nem teljes GUI/export throughput, és nem véletlengenerálás elleni összehasonlítás.
A GPU előnyére nincs mért állítás.

A CUDA fordító erőforrásjelentése: [test-results/cuda-build.txt](test-results/cuda-build.txt).
A Windows EXE PE32+ / x86-64 GUI bináris; statikus importjai kizárólag Windows
rendszer-DLL-ek: bcrypt, COMCTL32, GDI32, KERNEL32, msvcrt, ole32, SHELL32, USER32.
A CUDA és NVML futásidőben, opcionálisan töltődik be a driverből.

Nem ellenőrzött: natív Windows 10/11, az RTX 3050 tényleges futása/telemetriája,
driver JIT újabb GPU-kon, MSVC build, GitHub Actions távoli futása. A forrás és a
hardveres tesztprogram mellékelve van; ezekből nem következik már végrehajtott GPU-teszt.
