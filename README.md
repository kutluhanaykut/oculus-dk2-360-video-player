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
│   ├── Dk2WinUsb.*        # WinUSB'deki DK2'ye libusb ile doğrudan erişim
│   ├── OrientationFilter.*# jiroskop + yerçekimi yön filtresi
│   ├── ImuPacket.*        # DK1/DK2 IMU rapor çözücü
│   ├── DriverInstaller.*  # DK2 WinUSB sürücüsünü otomatik kurar (pnputil)
│   ├── FileDialog.*       # Windows dosya seçici
│   ├── Process.*          # harici process, UTF-8 ↔ wide dönüşümü
│   ├── Logger.*           # hem disk hem OutputDebugString
│   └── Projection.*       # küçük yardımcı projeksiyon dönüşümleri
├── resources/             # sürüm bilgisi ve DPI manifesti
├── scripts/               # bootstrap, build, run, package
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

## Çalıştırma

### PowerShell'den
```powershell
powershell -ExecutionPolicy Bypass -File scripts\run.ps1 -Configuration Release
```
veya doğrudan:
```powershell
.\build\bin\Release\DK2VRPlayer.exe
```

### Git Bash'ten (MSYS2/MINGW64)
> **Önemli:** Git Bash'te Windows tarzı ters eğik çizgili (`\`) yollar **kullanmayın**. Bash, `\b`, `\R`, `\D` gibi dizileri kaçış karakteri olarak yorumlar ve `build\bin\Release\DK2VRPlayer.exe` yazdığınızda `buildbinReleaseDK2VRPlayer.exe` gibi bozuk bir yol oluşur ("Windows cannot find" hatası). Bunun yerine **düz eğik çizgi** (`/`) kullanın:

```bash
./build/bin/Release/DK2VRPlayer.exe
```

veya `run.ps1` betiğini kullanın (her kabuktan güvenli):
```bash
powershell -ExecutionPolicy Bypass -File scripts/run.ps1 -Configuration Release
```

## Kullanım

1. DK2'yi bilgisayara bağlayın. Windows'un **genişletilmiş masaüstü** modunda, 1920×1080 (yatay) ve **75 Hz**'de bir ekran olarak tanıtılmalıdır (Rift Display için Oculus'un eski yazılımı veya Intel/Nvidia kenar boşluğu ayarı).
2. `DK2VRPlayer.exe`'yi çalıştırın. Pencere otomatik olarak 1920×1080 çözünürlüğe gelir.
3. **Yerel 360 video aç** düğmesiyle bir dosya seçin ya da pencereye sürükleyin; **YouTube** sekmesine bir 360 URL'si girip oynatın. Yerel video açıldığında projeksiyon modu önce dosya adı etiketlerine, yoksa en-boy oranına göre otomatik seçilir:
   - Dosya adında `_180` / `vr180` → **180 derece SBS 3D** (kare görüntüde **180 derece mono**)
   - `_TB`, `_OU`, `_3dv`, `top-bottom` → **3D 360 üst/alt**
   - `_LR`, `_SBS`, `_3dh`, `side-by-side` → **3D 360 yan yana**
   - `_EAC`, `cubemap` → **Cubemap (EAC)**
   - Etiket yoksa: **~1:1** → **3D 360 üst/alt**, **~3:2** → **Cubemap (EAC)**, diğerleri (2:1 dahil) → **Mono 360**
   - İstediğiniz modu **"360 video ve lens ayarları"** panelindeki **Projeksiyon** açılır listesinden veya **1-6** tuşlarıyla manuel olarak da seçebilirsiniz.
   - **180 derece modlarında** video yalnızca ön yarım küreye yansıtılır; başınızı 180°'den fazla çevirdiğinizde arka taraf siyah kalır (360° sarmalama yok).
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
| `4` | Cubemap (EAC) |
| `5` | 180 derece (mono) |
| `6` | 180 derece SBS 3D |
| `D` | DK2 lens distorsiyon düzeltmesini aç/kapa |

## DK2 bağlantı notları
- DK2'de iki kablo vardır: HDMI (veya DVI adaptör) ve USB. USB jiroskop için gereklidir; **konum kamerası** gerekmez (yazılım bunu kullanmaz).
- Yönelim verisi iki yoldan okunur: izleme cihazı Windows HID sürücüsündeyse **OpenHMD** (`drv_oculus_rift`), **WinUSB** sürücüsündeyse uygulamanın kendi **libusb** yolu. libusb yolu jiroskopu ivmeölçerle (yerçekimi) düzeltir; eğim/yuvarlanma kaymaz, yaw yavaşça kayabilir (`R` ile sıfırlanır). `DK2'yi yeniden tara` düğmesi cihazı yeniden açar.
- Windows ekran ayarlarında DK2'yi **genişletilmiş** ve **75 Hz** olarak ayarlamak en iyi deneyimi verir; tam ekran modu 75 Hz'e sabitlenmiştir.

### "Dahili jiroskop: DK2 BULUNAMADI" hatası çözümü
DK2 USB'de iki cihaz olarak görünür; ikisi de VID `2833`:

| PID | Cihaz | Sürücü |
|-----|-------|--------|
| `0021` | **Rift DK2** izleme cihazı (IMU/jiroskop) | WinUSB (önerilen) veya Windows HID |
| `2021` | DK2'nin içindeki **USB hub** | Windows'un kendi hub sürücüsü — **asla değiştirmeyin** |

> **Uyarı:** Zadig veya başka bir araçla PID `2021`'in (hub) sürücüsünü değiştirmek, arkasındaki izleme cihazının bağlantısını keser.

**Önerilen kurulum: izleme cihazı (PID `0021`) WinUSB'de.** SteamVR için `dk2vr` sürücüsü de bu kurulumu kullanır; ikisi aynı sürücüyle çalışır (aynı anda değil: cihazı bir seferde tek program açabilir).

1. **DK2'nin USB kablosunun bağlı olduğunu doğrulayın.** Aygıt Yöneticisi'nde "Rift DK2" (PID `0021`) görünmelidir.
2. **Zadig ile WinUSB kurun:**
   - [Zadig](https://zadig.akeo.ie/) aracını çalıştırın, "Options > List All Devices" seçin.
   - Listeden **Rift DK2**'yi seçin ve USB ID'nin `2833 0021` olduğunu doğrulayın.
   - Hedef sürücü olarak **WinUSB** seçip "Replace Driver"a tıklayın.
3. **Alternatif — uygulama içi kurulum:** Uygulamayı **Yönetici olarak** çalıştırıp "DK2 WinUSB sürücüsünü otomatik kur" düğmesine basın. Oluşturulan INF imzasız olduğundan Windows 11 bunu reddedebilir; bu durumda Zadig'i kullanın.
4. **Uygulamayı yeniden başlatın** ve "DK2'yi yeniden tara" düğmesine basın.

İzleme cihazı Windows'un varsayılan HID sürücüsünde kalırsa uygulama onu OpenHMD ile açar; bu da çalışır, ancak SteamVR `dk2vr` sürücüsü WinUSB bekler.

> **İpucu:** Uygulama, DK2VRPlayer.log dosyasına detaylı teşhis bilgisi yazar. DK2'nin USB cihazı bulunup bulunmadığını ve hangi sürücüye bağlı olduğunu bu logdan kontrol edebilirsiniz.

## Açık kaynak lisansları
Bu proje **MIT** lisansı ile dağıtılmaktadır. Üçüncü parti bileşenler (`libVLC`, `SDL2`, `GLM`, `nlohmann/json`, `OpenHMD`, `hidapi`, `GLEW`, `Dear ImGui`, `yt-dlp`) kendi lisanslarına sahiptir; ayrıntılar `THIRD_PARTY_NOTICES.md` içinde toplanmıştır.
