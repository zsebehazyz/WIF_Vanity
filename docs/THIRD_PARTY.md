# Külső komponensek

A kereső, a Windows GUI és a CUDA kernel saját projektforrás. A JeanLucPons/VanitySearch
README-jét referencia gyanánt tekintettük meg; abból forráskódot nem vettünk át.

A mellékelt MinGW kiadási binárisok statikusan linkelik a GCC libgcc/libstdc++
és MinGW-w64/winpthreads futtatókódjának szükséges részeit. A GCC futtatókönyvtárakra
GPLv3 + GCC Runtime Library Exception vonatkozik; a MinGW-w64 összetevők különféle
megengedő licenceket használnak. A fordításhoz használt Debian csomagok jogi
értesítéseit változatlanul mellékeljük a `licenses/` könyvtárban. Ezek egyes, a
binárisba nem kerülő fordítókomponenseket is felsorolhatnak.

- GCC: https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html
- GCC Runtime Library Exception: https://www.gnu.org/licenses/gcc-exception-3.1.html
- MinGW-w64: https://www.mingw-w64.org/license/

A CUDA kernelt NVIDIA CUDA Toolkit 13.1 fordítja; NVIDIA cudart DLL-t vagy
NVIDIA drivert nem terjesztünk a csomagban. Az alkalmazás a felhasználó által
telepített driver nyilvános CUDA Driver API-ját és opcionális NVML-jét használja.
A Windows CNG, Common Controls és egyéb Windows DLL-ek az operációs rendszer részei.
A Linux fejlesztői változat OpenSSL-t használ független SHA256 ellenőrzésre;
OpenSSL bináris nem része a Windows csomagnak.
