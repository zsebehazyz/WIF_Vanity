# Eredet és licencek

## orlp/ed25519

https://github.com/orlp/ed25519

Ellenőrzött és rögzített commit: `b1f19fab4aebe607805620d25a5e42566ce46a0e`.
Zlib licenc; szerző Orson Peters, 2015. A változatlan CPU-forrás a
`third_party/ed25519/` könyvtárban, a licenc ugyanott `LICENSE.txt` néven szerepel.
A SHA512 fájl Tom St Denis / LibTomCrypt eredetmegjelölését is megőriztük.

A `tools/prepare_cuda.py` ebből generál **módosított**, egyértelműen megjelölt
CUDA-változatot a build könyvtárba. Eltérés: device függvényminősítők, konstans
memóriába helyezett táblák és ekvivalens, 16 szavas gördülő SHA512 ütemezés.
A CUDA-adaptált kód hoston is tesztelődik, független Monocypher eredményekkel.
Ez nem egyenértékű tényleges CUDA hardveres futtatással.

## Monocypher 4.0.2

https://github.com/LoupVaillant/Monocypher

Rögzített commit: `0d85f98c9d9b0227e42cf795cb527dff372b40a4` (4.0.2 tag).
A core és az optional Ed25519 modul változatlan forrása a
`third_party/monocypher/` könyvtárban található. BSD-2-Clause vagy CC0 kettős licenc;
a BSD licencet használjuk. Teljes értesítés: `LICENSE.md` és a forrásfejlécek.

A Monocypher a keresőtől független CPU-validációt adja; GPU-s jelölteket nem
állítunk elő vele. A CPU/CUDA kereső az orlp implementációjára épül.

## Fordítók és operációs rendszer

A MinGW binárisok statikusan linkelt GCC/libstdc++/libgcc és MinGW-w64 runtime
részeket tartalmaznak. Licencértesítések a fő projekt `docs/licenses/` könyvtárában,
a Windows ZIP-ben külön `licenses/` alatt. GCC Runtime Library Exception:
https://www.gnu.org/licenses/gcc-exception-3.1.html .

A CUDA Toolkitet csak fordításra használjuk; NVIDIA drivert vagy cudartot nem
terjesztünk. A Windows CNG, DPAPI és GUI DLL-ek az operációs rendszer részei.
A solders 0.29.0 kizárólag fejlesztési interoperabilitási tesztfüggőség;
sem Python, sem solders nincs az EXE futtatási függőségei között.

## Pons és Solanity

A Pons-féle VanitySearch referencia a batchelt GPU-munkaszervezéshez, de
secp256k1-kódja nem része ennek az Ed25519 programnak.
A https://github.com/mcf-rocks/solanity README-jét is megvizsgáltuk
(`c00a074a859d9517f19b627df500ce8b25320fac`), de abból nem vettünk át kódot
vagy véletlenszám-generálást. Az upstream orlp és a Monocypher rögzített,
fent felsorolt fájljait használtuk.
