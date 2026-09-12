# Windows'ta Derleme

Bu proje şu an bir Linux sandbox'ında, **Qt6 ve librtlsdr kurulu olmadan**
geliştirildi (bkz. `docs/ROADMAP.md` — hangi hedeflerin orada derlenip test
edildiği). Gerçek masaüstü uygulamasını (GUI + canlı RTL-SDR girişi) ilk kez
sizin Windows makinenizde derleyip test etmeniz gerekecek.

## Gerekli araçlar

1. **Visual Studio 2022** (Desktop development with C++ iş yükü) — ya da
   MSYS2/MinGW-w64, tercihinize göre.
2. **CMake** ≥ 3.21 (https://cmake.org/download/)
3. **vcpkg** (bağımlılık yönetimi için önerilir):
   ```powershell
   git clone https://github.com/microsoft/vcpkg
   .\vcpkg\bootstrap-vcpkg.bat
   ```
4. Bağımlılıkları vcpkg ile kurun:
   ```powershell
   .\vcpkg\vcpkg install qt6-base qt6-multimedia sqlite3 rtl-sdr --triplet x64-windows
   ```
   - `rtl-sdr` paketi zaten sizin RTL-SDR/Realtek dongle'ınız için gereken
     `librtlsdr`'ı sağlar. SDRSharp kurulumunuzdaki Zadig/WinUSB sürücü
     ayarının bu paketle de uyumlu olması gerekir (aynı libusb tabanlı yol).
   - Qt6'yı isterseniz doğrudan Qt'nin resmi Windows yükleyicisiyle de
     kurabilirsiniz (https://www.qt.io/download-qt-installer) — bu durumda
     vcpkg'den `qt6-base`/`qt6-multimedia`'yı atlayıp CMake'e
     `-DCMAKE_PREFIX_PATH=<Qt kurulum yolu>/lib/cmake` verin.

## Derleme

```powershell
cmake -B build -S . `
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg yolu>/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config RelWithDebInfo -j
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

CMake yapılandırma çıktısında şu satırları görmelisiniz (aksi halde ilgili
hedef atlanır — bkz. üstteki bağımlılık kurulumu):

```
-- Qt6 found (...) -> GUI target 'biem_gui' will be built
-- librtlsdr found -> live RTL-SDR hardware backend enabled
```

## Projeyi bu repoya bağlama

```powershell
cd D:\Projects
git clone https://github.com/mcelik3737/Biem_SDR_V1.git
```

(`git clone` komutu `D:\Projects\Biem_SDR_V1` klasörünü kendisi
oluşturur - önceden klasör açıp içine girmeyin, iç içe kopya oluşur.)

## İlk gerçek donanım testi için öneri

1. Önce `biem_cli` ve `tests` hedeflerini (Qt/rtlsdr olmasa da) derleyip
   `ctest` ile sentetik sinyal testlerinin geçtiğini doğrulayın — bu,
   Windows derleyicinizin/CMake kurulumunuzun sorunsuz olduğunu kanıtlar.
2. Sonra `librtlsdr` + gerçek dongle ile `RtlSdrSource`'u tek bir bilinen
   analog FM frekansında (örn. bir PMR simpleks kanalı) deneyin —
   `docs/ROADMAP.md`'deki "sonraki adım" maddelerine bakın.
3. DMR/repeater kısımlarını, `docs/DMR_NOTES.md` ve
   `docs/HYTERA_HR659.md`'deki doğrulama adımları tamamlanmadan üretimde
   güvenmeyin.
