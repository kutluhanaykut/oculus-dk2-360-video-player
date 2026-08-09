# Üçüncü Parti Bileşenler

Bu belge, `DK2VRPlayer` çalıştırılabilir dosyası içinde veya üzerinde bağımlı
olunan açık kaynak bileşenlerin lisanslarını özetler. Her madde, ilgili projenin
resmi kaynağına yönlendirme yapar; kapsamlı metin için kaynak dağıtımlarına
bakın.

| Bileşen | Sürüm | Lisans | Kaynak |
|---------|-------|--------|--------|
| libVLC (VideoLAN) | 3.0.21 | LGPL-2.1+ | https://download.videolan.org/pub/videolan/vlc/3.0.21/ |
| yt-dlp | latest | Unlicense (public domain) | https://github.com/yt-dlp/yt-dlp |
| SDL2 | release-2.30.11 | zlib | https://github.com/libsdl-org/SDL |
| OpenGL Mathematics (GLM) | 1.0.1 | MIT | https://github.com/g-truc/glm |
| nlohmann/json | 3.11.3 | MIT | https://github.com/nlohmann/json |
| GLEW (Perlmint fork) | 2.2.0 | MIT / Khronos | https://github.com/Perlmint/glew-cmake |
| Dear ImGui | 1.91.6 | MIT | https://github.com/ocornut/imgui |
| OpenHMD | 0.3.0 | Boost 1.0 | https://github.com/OpenHMD/OpenHMD |
| hidapi | 0.14.0 | BSD-3-Clause / GPL-2.0 (Windows) | https://github.com/libusb/hidapi |

libVLC Windows derlemesi LGPL kapsamında yeniden dağıtılır; `VLC-COPYING.txt`
taşınabilir pakete dahil edilir. yt-dlp kamu malıdır; herhangi bir kısıtlama
uygulanmaz.

Bu projeyi derlerken, yukarıdaki bileşenler ilk seferde `scripts/bootstrap.ps1`
üzerinden indirilir ve FetchContent üzerinden yerel kaynak ağacından
derlenir. Ticari kullanım için lisans koşullarını doğrulamak kullanıcının
sorumluluğundadır.
