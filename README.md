# DK2 360 VR Player

Açık kaynak kodlu, yalnızca **Oculus Rift DK2** için tasarlanmış, **Windows 11** üzerinde çalışan bir **360° / VR** video oynatıcısı. Konum kamerası olmadan, sadece gözlüğün dahili jiroskop/IMU verisiyle yönelim takibi yapar; yerel dosyaları ve YouTube 360 videolarını oynatabilir.

## Öne çıkan özellikler
- **libVLC 3.x** (yt-dlp eşliğinde) sayesinde yüzlerce video konteynerini ve DRM'siz YouTube 360 kaynaklarını oynatır.
- **OpenHMD 0.3** ile DK2'nin **dahili jiroskop/IMU** verisi okunur; başlık yönüne göre küre içi görüntü gerçek zamanlı işlenir. Harici konum kamerası gerektirmez.
- Özel OpenGL 3.3 küre shader'ı; mono, top/bottom ve yan yana 3D formatları; DK2'ye özgü lens distorsiyon düzeltme (K1/K2 + kromatik aberasyon) shader'ı.
- **SDL2 2.30** ile pencere yönetimi, çoklu ekran seçimi ve tam ekran DK2 çıkışı.
- **Dear ImGui** ile Türkçe arayüz; dosya tarayıcısı, YouTube URL çözücü, lens katsayıları, ses/renk/distorsiyon ayarları.
- Hepsi statik bağlanmış tek bir **Windows x64 EXE** olarak paketlenir.

## Proje yapısı
```
.
├── CMakeLists.txt         # CMake + FetchContent ile derleme betiği
├── src/                   # C++20 uygulama kodu
│   ├── main.cpp           # WinMain girişi
│   ├── Application.*      # SDL, ImGui, VR tam ekran ve olay döngüsü
│   ├── Renderer.*         # OpenGL küre, lens distorsiyon ve VR framebuffer'ları
│   ├── VideoPlayer.*      # libVLC video kuyruğu (RV32 callback'leri)
│   ├── VlcApi.*           # libvlc.dll dinamik yükleyici
│   ├── YouTubeResolver.*  # yt-dlp üzerinden video/ses URL çözümü
│   ├── HmdManager.*       # OpenHMD jiroskop erişimi
│   ├── FileDialog.*       # Windows dosya seçici
│   ├── Process.*          # harici process, UTF-8 ↔ wide dönüşümü
│   ├── Logger.*           # hem disk hem OutputDebugString
│   └── Projection.*       # küçük yardımcı projeksiyon dönüşümleri
├── resources/             # sürüm bilgisi ve DPI manifesti
├── scripts/               # bootstrap, build, package
├── tests/                 # Projection birim testleri (CTest)
└── third_party/           # bootstrap ile indirilen VLC ve yt-dlp
```

## Derleme
Gereksinimler:
- Windows 10/11
- **Visual Studio 2022 Community** (C++ masaüstü + Win10 SDK bileşenleri) **veya** Build Tools 2022 (MSVC v143)
- **CMake 3.24+**
- **Git** (FetchContent ve bootstrap için)
- İnternet erişimi (yalnızca ilk derleme)

> Not: `scripts\build.ps1` doğrudan `cmake`'i çağırır; `ninja` veya başka bir jeneratöre gerek yoktur (Visual Studio generator kullanır).

### 1) Bağımlılıkları indir
```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap.ps1
```
Bu komut `third_party/vlc/` içine **libVLC 3.0.21**'in resmi Windows derlemesini, `third_party/yt-dlp.exe` dosyasını indirir. SHA-256 doğrulaması yapılır.

### 2) Derle, test et
```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Configuration Release
```
İlk derleme ~5-8 dakika sürer (FetchContent ile tüm bağımlılıklar indirilir). `build\bin\Release\DK2VRPlayer.exe` çıktısını verir.

### 3) Portable paket üret
```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Configuration Release -Package
```
`dist\DK2-360-VR-Player-win64.zip` dosyasını oluşturur; EXE, libVLC DLL'leri, eklentiler ve yt-dlp dahil.

## Kullanım
1. DK2'yi bilgisayara bağlayın. Windows'un **genişletilmiş masaüstü** modunda, 1920×1080 (yatay) ve **75 Hz**'de bir ekran olarak tanıtılmalıdır (Rift Display için Oculus'un eski yazılımı veya Intel/Nvidia kenar boşluğu ayarı).
2. `DK2VRPlayer.exe`'yi çalıştırın. Pencere otomatik olarak 1920×1080 çözünürlüğe gelir.
3. **Yerel 360 video aç** düğmesiyle bir dosya seçin ya da pencereye sürükleyin; **YouTube** sekmesine bir 360 URL'si girip oynatın.
4. **DK2 ekranında VR tam ekran (F11)** düğmesi (veya `F11` kısayolu) seçili HDMI ekranını tam ekran yapar, fare imleci gizlenir ve OpenHMD üzerinden okunan yönelim ile stereo görüntü hesaplanır. **Esc** veya `F11` ile geri dönülür.

### Klavye kısayolları
| Tuş | İşlev |
|-----|-------|
| `F11` | DK2'de stereo tam ekran ↔ pencere |
| `Esc` | VR modundan çık veya uygulamayı kapat |
| `Space` | Oynat / duraklat |
| `R` | Bakışı yeniden merkezle |
| `←` / `→` | 10 saniye geri / ileri |
| `↑` / `↓` | Ses aç / kapa |
| `1` / `2` / `3` | Mono 360 / Top-Bottom 3D / Side-by-Side 3D |
| `D` | DK2 lens distorsiyon düzeltmesini aç/kapa |

## DK2 bağlantı notları
- DK2'de iki kablo vardır: HDMI (veya DVI adaptör) ve USB. USB jiroskop için gereklidir; **konum kamerası** gerekmez (yazılım bunu kullanmaz).
- Yönelim verisi OpenHMD'nin `drv_oculus_rift` sürücüsüyle okunur. `DK2'yi yeniden tara` düğmesi, cihazı yazılıma yeniden tanıtır.
- Windows ekran ayarlarında DK2'yi **genişletilmiş** ve **75 Hz** olarak ayarlamak en iyi deneyimi verir; tam ekran modu 75 Hz'e sabitlenmiştir.

## Açık kaynak lisansları
Bu proje **MIT** lisansı ile dağıtılmaktadır. Üçüncü parti bileşenler (`libVLC`, `SDL2`, `GLM`, `nlohmann/json`, `OpenHMD`, `hidapi`, `GLEW`, `Dear ImGui`, `yt-dlp`) kendi lisanslarına sahiptir; ayrıntılar `THIRD_PARTY_NOTICES.md` içinde toplanmıştır.
