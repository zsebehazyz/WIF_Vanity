# A célzott keresés matematikája

A WIF nem tetszőleges Base58 szöveg: a mainnet payload `0x80 || key32` vagy
`0x80 || key32 || 0x01`. Ehhez járul a SHA256(SHA256(payload)) első négy bájtja.
Az így kapott 37/38 bájtot egyetlen nagy, big-endian számként Base58-ba írjuk.
A mainnet uncompressed WIF-ek 51, a compressed WIF-ek 52 karakteresek.

Legyen a rögzített Base58 prefix értéke P, a változó végződés hossza s. Az összes
ilyen végződés pontosan a következő zárt számintervallumot fedi le:

```
L = P * 58^s
H = (P + 1) * 58^s - 1
```

A program 320 bites, fix méretű, egész aritmetikát használ; nincs lebegőpontos
kerekítés a határokban. A `prefix + '1'*s` és `prefix + 'z'*s` dekódolása
pontosan L és H. Az intervallumot előbb metszi a mainnet verzió és a secp256k1
kulcstartomány által megengedett legkisebb/legnagyobb nyers számmal.

Uncompressed esetben a nyers szám:

```
N = (0x80 * 2^256 + key) * 2^32 + checksum(key)
```

Compressed esetben:

```
N = ((0x80 * 2^256 + key) * 256 + 1) * 2^32 + checksum(key, 0x01)
```

A byte-határok szerinti lefelé vetítésből megkapjuk a legkisebb és legnagyobb
szóba jövő kulcsot. Mindkét peremkulcsot megtartjuk: a checksum és compressed
jelölő miatt a vetítés legfeljebb peremjelölteket vehet fel feleslegesen, valódi
megoldást nem hagyhat el. Minden kulcsból kiszámítjuk a valódi checksumot, és
ellenőrizzük, hogy N ténylegesen [L,H]-ban van-e. Ez a teljes Base58 újrakódolás
nélkül pontosan ugyanazt a prefixfeltételt adja. A találatokat újrakódoljuk és
második, független CPU modullal dekódoljuk/validáljuk.

Uncompressed intervallumonként nagyjából `58^s / 2^32` kulcsot kell vizsgálni;
compressed esetben `58^s / 2^40` kulcsot, plusz legfeljebb két peremkulcsot.
Például 6 karakter végződés 38 068 692 544 szöveget, de uncompressed esetben
csak körülbelül 8,86 kulcsnyi intervallumot jelent. Ez nem mért sebességnövekedés,
hanem a redundáns szövegtér matematikai összevonása. Nincs véletlengenerálás.

A `K`, `L` stb. a kitöltő helyén áll, nem a checksum helyett. Az utolsó s Base58
karakter a teljes érvényes WIF része; nem külön, szabadon megválasztható checksum.
Nem állítjuk, hogy s bármilyen választása esetén lesz megoldás.

## Pozíciók és prioritás

Uncompressed fejlécek: `5H`, `5J`, `5K`; compressed fejlécek: `K`, `L`.
A terv rögzíti az elválasztókból és a változatlan kifejezésből álló szakaszt,
a többi prefixpozíciót az adott kitöltővel tölti. „Minden pozíció” esetén az összes
lehetséges kezdőpozíciót vizsgálja; a kifejezés a kötelező WIF-fejléccel is
átfedhet, ha a karakterek egyeznek. Kikapcsolva közvetlenül a fejléc után kezdődik.
A kitöltésszámba a fejléc és a rögzített kifejezés/elválasztók nem számítanak bele.

A checksumos numerikus tartományon kívüli mintákat kihagyjuk. Az azonos
prefix/végződéshossz feladatokat összevonjuk; a különböző feladatokban esetleg
újra előforduló találatot WIF szerint deduplikáljuk. Sorrend: több kitöltő,
rövidebb végződés, a megadott kitöltők sorrendje, majd stabil fejléc/pozíciósorrend.
A korábbi feladatok sorszáma és az aktuális feladat kulcsoffsetje elegendő a
folytatáshoz. Az algoritmus változtatásakor az állapotformátumot is verziózni kell.

## CUDA és korlátok

A kernel egy szálhoz egy 256 bites kulcsot rendel, két egyblokkos SHA256-ot és
pontos nyers intervallumtesztet végez. A Base58-határok CPU-n előre számolódnak;
GPU-n nincs drága szövegdekódolás. Legfeljebb 65 536 jelöltes batchenként csak
a fix paramétereket, a találatszámot és a találati indexeket mozgatjuk.
A device pufferek újrafelhasználtak. Warp-szintű ballot/shuffle csökkenti a
találatszámláló atomikus műveleteit. A GUI külön szálból, 250 ms-onként olvas.

A STOP batchhatáron áll meg, így nem veszíti el a feldolgozott batch eredményét.
A találati korlát batch közepén is megállíthatja az eredmények elfogyasztását;
az offset az utolsó elfogyasztott jelölt után áll, ezért a folytatás nem hagy ki
jelöltet. A vizsgáltjelölt-számláló a valóban lefuttatott teljes batchméretet méri;
a korlát miatt újra lefuttatott batchrész ismét beleszámít.

4–12 közötti végződéshossz, legfeljebb 100 000 tárolt találat támogatott. A teljes
12 karakteres terv igen nagy lehet. A GUI a korlátot feltünteti, nincs teljesítmény-
vagy lefutásiidő-garancia. Rövid intervallumokra a CPU gazdaságosabb lehet.
Az RTX 3050-en még nem mértünk sem sebességet, sem foglaltságot, sem maximális
kernelkésleltetést; GPU-CPU paritástesztet mellékelünk a hardveres ellenőrzéshez.

## Pons VanitySearch

Referencia: https://github.com/JeanLucPons/VanitySearch,
megtekintett HEAD: `c8d48ce5f03f5357c0e87cbdb3e1e93cd50af88b`.
Ez Bitcoin **cím**kereső: elliptikusgörbe-műveletei nem szükségesek a WIF
privátkulcs-szövegének kereséséhez. A fix méretű aritmetika és a batchelt CUDA
feldolgozás hasznos elv, de a projekt nem másol belőle forráskódot, és nem állítja,
hogy annak GPU-optimalizálási vagy biztonsági tulajdonságait örökölte.
