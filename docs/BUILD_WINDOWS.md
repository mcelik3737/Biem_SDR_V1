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
   .\vcpkg\vcpkg install qt6-base qt6-multimedia sqlite3 rtlsdr --triplet x64-windows
   ```
   - vcpkg'deki paket adı **`rtlsdr`** (tiresiz) — `rtl-sdr` diye bir port
     yok, denerseniz "does not exist" hatası alırsınız.
   - `rtlsdr` paketi zaten sizin RTL-SDR/Realtek dongle'ınız için gereken
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

## GUI'yi (`biem_gui`) ilk kez deneme

`biem_gui`, `Qt6 found` ile derlendiyse `build/src/RelWithDebInfo/biem_gui.exe`
(veya seçtiğiniz config'e göre) olarak çıkar. Açılışta 3 sekme gelir:

- **Canlı Dinleme** — YENİ: mod (Analog FM / DMR), frekans, squelch eşiği,
  kazanç seçip **Baslat**'a basınca gerçek RTL-SDR'dan alım başlar; güç
  seviyesi, squelch açık/kapalı, (DMR modunda) kilit VAR/yok ve aktif çağrı
  durumu canlı güncellenir, her olay alttaki günlük panelinde birikir -
  `biem_cli live`/`dmr-live`'ı terminalde okumanın GUI karşılığı. Biten bir
  çağrı otomatik olarak "Cagri Kayitlari" sekmesinde belirir.
- **Cagri Kayitlari** — geçmiş çağrıları arayın, seçip **Dinle** ile
  oynatın.
- **Kanallar** — kanal listesi düzenleyici (henüz sadece bellek içi -
  kalıcı değil, bkz. `docs/ROADMAP.md` Faz 2).

RTL-SDR takılı değilse veya `librtlsdr` bu derlemede bulunamadıysa
**Baslat** düğmesi devre dışı kalır / net bir hata mesajı gösterir -
donanım yokken sessizce çökmemesi gerekir. Eğer çökerse veya başka bir
şekilde beklenmeyen davranış görürseniz, tam terminal çıktısını paylaşın.
