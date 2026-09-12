# Yol Haritası / Durum

Bu dosya her oturumda güncellenmeli — "ne bitti, ne yarım, sırada ne var,
kullanıcıdan ne bekleniyor" tek bakışta görülsün diye.

## Faz 0 — İskelet (bu oturumda tamamlandı)

- [x] Yeni repo (`mcelik3737/Biem_SDR`), CMake tabanlı C++20 proje iskeleti
- [x] `core/`: CallRecord, ChannelConfig, Database (SQLite), CallRecorder, WavWriter
- [x] `dsp/`: IqSource arayüzü, WavIqSource (dosya/sentetik kaynak), NBFM demodülatör + squelch
- [x] `dsp/RtlSdrSource`: librtlsdr tabanlı, CMake'te opsiyonel (bu sandbox'ta derlenmedi — kütüphane yok)
- [x] `dsp/dmr/`: FEC primitifleri (Hamming(15,11,3), Golay(20,8,7), BPTC(196,96) interleave),
      frame sync (senkron kelimeleri web'den doğrulandı), slot/LC decode iskeleti
- [x] `net/`: INetworkIngestSource arayüzü, UdpRawLogger (tam çalışır), HyteraHR659Source (stub)
- [x] `ui/`: Qt6 MainWindow + ChannelList + CallLog(arama) + Playback (bu sandbox'ta derlenmedi — Qt6 yok)
- [x] `cli/`: biem_cli — Qt'siz headless kayıt aracı
- [x] `tests/`: FEC round-trip, NBFM sentetik ton, Database CRUD/arama
- [x] Linux sandbox'ta gerçek derleme + test (Qt6/librtlsdr olmadan): **bkz. aşağıdaki "Bu oturumda doğrulanan" bölümü**

## Bu oturumda gerçekten doğrulanan (derleyici + ctest ile)

Linux sandbox'ta (Ubuntu 24.04, GCC 13.3, Qt6/librtlsdr kurulu değil):

- `cmake -B build` → SQLite3 bulundu; Qt6 ve librtlsdr beklendiği gibi
  "NOT found" ve ilgili hedefler (`biem_gui`, `biem_sdr_rtl`) atlandı.
- `cmake --build build` → `biem_core`, `biem_cli`, `test_fec`, `test_nbfm`,
  `test_database` temiz derlendi. Ayrıca `-Wall -Wextra -Wpedantic
  -Wshadow` ile ayrı bir Debug derlemesi de **hic warning uretmeden** gecti.
- `ctest --output-on-failure` → ilk çalıştırmada `test_fec` 8 kontrolde
  **gerçekten başarısız oldu** (`DmrFrameSync` senkron penceresi bir bit
  kaymıştı — bkz. `DmrFrameSync.cpp` yorumu); düzeltildikten sonra
  **3/3 test PASSED** (`test_fec`, `test_nbfm`, `test_database`).
- `biem_cli demo` → sentetik FM sinyali → NBFM demod → squelch → gerçek bir
  `.wav` dosyası (44-byte RIFF header + 48000 byte PCM16 = 3.000s, `file`
  komutuyla "RIFF WAVE PCM 16 bit mono 8000 Hz" olarak doğrulandı) →
  SQLite'a kayıt → `biem_cli search` ile geri okuma, hepsi uçtan uca
  çalıştı.
- `biem_cli udp-capture` → gerçek bir Python soketinden 2 UDP paketi
  gönderilip alındı, hem `.hexlog` (okunabilir) hem `.raw` (uzunluk
  önekli binary) dosyalarına doğru şekilde yazıldığı byte-byte doğrulandı.

**Doğrulanmayan** (bu ortamda mümkün değil): Qt6 GUI derlemesi, gerçek
RTL-SDR donanımı, gerçek DMR/HR659 sinyaliyle test — bunlar Windows'ta ilk
gerçek denemede yapılmalı (bkz. `docs/BUILD_WINDOWS.md`).

## Windows'ta gerçek donanımla doğrulanan (kullanıcının kendi PC'sinde)

Bu, projenin ilk gerçek RF testiydi — sentetik sinyal değil:

- vcpkg (`sqlite3`, `rtlsdr`) + MSVC (Visual Studio, CMake 4.4.3) ile temiz
  derleme. Süreçte 3 gerçek, birbirinden farklı hata bulundu ve düzeltildi:
  yanlış vcpkg port adı (`rtl-sdr` değil `rtlsdr`), CMake'in pkg-config'siz
  ortamlarda (çıplak Windows) librtlsdr'ı hiç bulamaması (fallback eklendi),
  ve `biem_sdr_rtl`'in RTLSDR bağımlılıklarını yanlışlıkla `PRIVATE`
  linklemesi (`PUBLIC` yapılınca düzeldi — bu üçüncüsü Linux'ta hiç
  görünmüyordu çünkü sistem kütüphanesi zaten varsayılan linker yolunda).
- **Gerçek RTL-SDR donanımı** (Elonics E4000 tuner'lı bir USB dongle)
  `RtlSdrSource` ile açıldı, 446.00625 MHz'de (PMR kanal 1, 12.5 kHz)
  gerçek zamanlı I/Q akışı alındı.
- **Gerçek bir el telsizinden** aynı frekansa verilen sinyal: boş kanal
  ~-27..-41 dB dalgalanırken, PTT basılı konuşma sırasında sinyal **+2.9 dB
  civarında, çok kararlı** bir seviyeye çıktı (~30 dB S/N marjı) - gerçek
  bir taşıyıcının imzası.
- Squelch eşiği (-15 dB, gözlemlenen seviyelere göre elle seçildi) ile
  **iki ayrı PTT basışı iki ayrı çağrı olarak doğru segmentlendi** (4915 ms
  ve 3822 ms), her ikisi de `.wav` olarak diske yazıldı ve `biem_cli
  search` ile veritabanından doğru şekilde geri okundu.
- Ses kalitesi (gerçek konuşmanın anlaşılırlığı) dinlenerek doğrulanacak -
  bkz. Faz 1 altında ilgili madde.

Yani: RTL-SDR → NBFM demod → squelch tabanlı çağrı segmentasyonu → WAV +
SQLite → arama zincirinin **tamamı artık gerçek donanım ve gerçek RF
sinyaliyle uçtan uca doğrulanmış durumda.**

## "Tık tık" (modülasyon yok) raporu - kök neden + düzeltme

Yukarıdaki gerçek donanım testinden sonra kullanıcı, kayıtlarda konuşma
yerine yalnızca tık tık gibi bir ses olduğunu, modülasyonun hiç
duyulmadığını bildirdi. İlk teori (discriminatörden önce kanal filtresi
eksikliği, `9b3cd10`) kendi kendine yapılan doğrulamada sentetik gürültüye
karşı ölçülebilir bir iyileşme göstermedi - kullanıcıya da bu sınırlama
dürüstçe belirtildi. Kullanıcı çalışan bir referans işaret etti: aynı
kullanıcının Python ile yazdığı (ayrı `mcelik3737/biem_sdr` reposundaki)
çalışan analog FM alıcısı.

**Gerçek kök neden** (Python referansıyla karşılaştırarak bulundu): Python
kodu donanımı hedef kanalın TAM ÜZERİNE değil, kasıtlı olarak kanaldan
uzağa (`center_hz`) ayarlıyor, sonra farkı (`channel.frequency_hz -
center_hz`) yazılımda dijital mikser ile geri kaydırıyor
(`dsp.py`'deki `FMDemodulator.process`). Bizim C++ kodumuz ise donanımı
doğrudan hedef frekansa ayarlıyordu. Bunun önemi: E4000 gibi zero-IF
tuner'lar, ayarlandıkları frekansın TAM ÜZERİNDE bir DC spike/LO sızıntısı
üretir. Kanalın tam üzerine ayar yapılırsa bu spike istenen sinyalle AYNI
frekansta çakışır - hiçbir filtre aynı frekanstaki iki şeyi ayıramaz. Bu,
yazılımda düzeltilemeyen bir donanım/ayar hatasıdır; tek çözüm donanımı
kanaldan uzağa ayarlayıp farkı yazılımda geri kaydırmaktır.

**Uygulanan düzeltme** (`NbfmDemodulator`'a `mixerOffsetHz` alanı +
sürekli-fazlı dijital mikser/NCO, artı discriminatör sonrası DC-blocker):

- `biem_cli.cpp`'nin `runLive()`'ı artık donanımı `frequencyHz -
  mixerOffsetHz` (varsayılan 50 kHz altı) frekansına ayarlıyor, `cfg`'de
  `mixerOffsetHz`'i buna eşit set ediyor.
- `NbfmConfig::mixerOffsetHz` **varsayılan olarak 0 (kapalı)** - sadece
  gerçek RTL-SDR donanım yolu (`runLive`) bunu açıkça set ediyor. Bunun
  nedeni: sentetik testler, `biem_cli demo` ve dosyadan tekrar oynatma gibi
  zaten baseband'de IQ üreten/okuyan tüm çağıranlar için mikser anlamsız -
  varsayılan sıfır olmasaydı bunlara yanlışlıkla bir kayma karışırdı (ilk
  denemede tam olarak bu oldu: varsayılan 50 kHz iken `demo` komutu ve
  mevcut testler IQ örnekleme hızlarına göre Nyquist'i aşan anlamsız bir
  kaymayla bozulurdu - fark edilip düzeltildi).

**Doğrulama (dürüst, sınırları belirtilmiş)**: Linux sandbox'ta bu sefer
gerçekten AYIRT EDEN bir sentetik test eklendi
(`testNbfmRecoversAudioDespiteDcSpikeWithMixerOffset`, `tests/test_nbfm.cpp`) -
gerçek DC spike'ı taklit eden sabit (0 Hz) güçlü bir karmaşık sapma +
istenen FM sinyalini +50 kHz'e yerleştirip, doğru `mixerOffsetHz` ile geri
kazanımı ton-korelasyonu (basit sıfır-geçiş sayımından çok daha katı bir
ölçüt - ilk denemede sıfır-geçiş sayımının bu senaryoyu AYIRT ETMEDİĞİ
görüldü, bu yüzden değiştirildi) ile ölçüyor. Düzeltme açıkken korelasyon
~0.98, kasıtlı olarak `mixerOffsetHz=0` yapılıp yeniden çalıştırıldığında
~0.77 (test eşiği 0.85 - ikisini de gerçekten ayırt ediyor, bu manuel
olarak her iki durumda da çalıştırılıp doğrulandı). `ctest` ile 3/3 test
PASSED, `-Wall -Wextra -Wpedantic -Wshadow` ile de temiz derleniyor.

**Doğrulanmayan / hâlâ açık**: Bu tamamen sentetik bir doğrulama - gerçek
E4000 DC spike'ının genliği/şekli farklı olabilir, ve "kanalın tam üzerine
ayarlanmış donanım + üzerine binen gerçek spike" senaryosu yazılımda hiç
tekrar üretilemez (yukarıda açıklandığı gibi, bu durumda ikisi matematiksel
olarak aynı frekansta olduğu için düzeltilemez - tek çözüm zaten uygulanan
"kanaldan uzağa ayarla" yaklaşımı). Yani bu, önceki teoriden çok daha güçlü
ve kanıta dayalı bir teşhis (kanıtlanmış çalışan bir referans
implementasyonuyla doğrudan karşılaştırmaya dayanıyor) ama **gerçek
donanımda henüz test edilmedi** - kullanıcının bir sonraki gerçek RF
denemesinde doğrulanması gerekiyor.

## Faz 1 — Sırada (kullanıcıdan girdi bekleyen)

- [ ] **Ses kalitesi doğrulaması (öncelikli - mixerOffsetHz düzeltmesi
      sonrası tekrar test)**: yukarıdaki "tık tık" raporu üzerine
      `mixerOffsetHz` mikser + DC-blocker düzeltmesi eklendi (bkz. ilgili
      bölüm yukarıda) ama gerçek donanımda henüz doğrulanmadı - kullanıcı
      güncel kodla yeni bir gerçek kayıt alıp dinleyerek konuşmanın
      anlaşılır olup olmadığını teyit etmeli. Hâlâ anlaşılmazsa/tık tık
      sürüyorsa bir sonraki şüpheli: `NbfmDemodulator.h`'daki "known
      simplifications" - basit tek-kutuplu decimation filtresi.
- [ ] **Hytera HR659 protokolü**: port numarası/numaraları + gerçek bir UDP
      yakalaması (`UdpRawLogger` ile) ya da resmi Hytera protokol dokümanı.
      Bkz. `docs/HYTERA_HR659.md`.
- [ ] **DMR burst bit ofsetlerinin doğrulanması**: ETSI TS 102 361-1
      tablolarıyla ya da bilinen-doğru bir açık kaynak referansla (OP25 /
      dsd-fme) bit-bit karşılaştırma. Bkz. `docs/DMR_NOTES.md`.
- [ ] **DMR ses (AMBE+2) decode kararı**: hangi harici decoder/lisans
      yaklaşımıyla ilerlenecek — kullanıcıyla netleştirilecek.
- [x] ~~Windows'ta ilk gerçek derleme~~ — yukarıda, tamamlandı.
- [ ] **Qt6 GUI'nin ilk gerçek derlemesi** — henüz denenmedi (şu ana kadar
      hep `biem_cli` ile headless test edildi).

## Faz 2 — GUI/UX detayları (henüz başlanmadı)

- [ ] Kanal listesi yönetimi (frekans, bant genişliği, mod, etiket) için
      kalıcı yapılandırma dosyası/ekranı
- [ ] Çoklu kanal eşzamanlı izleme (birden fazla RTL-SDR dongle ya da tek
      dongle + geniş bant + kanalize etme)
- [ ] Canlı seviye göstergesi / spektrum (opsiyonel, "beauty" için değil,
      operatörün sinyal var/yok görmesi için)
- [ ] Çağrı başlığı (Title) üretim kuralı: kullanıcıdan örnek/kural istenmeli
      (örn. "Grup adı + saat" mi, yoksa özel bir eşleme tablosu mu?)

## Açık sorular (kullanıcıya sorulacak / onay bekleyen)

1. Çağrı "Title" alanı tam olarak neye göre üretilsin? (Grup adı ↔ TG ID
   eşleme tablosu var mı, yoksa biz mi bir varsayılan format önerelim?)
2. Ses dosyası formatı tercihi: WAV (basit, büyük) mi, FLAC/Opus
   (sıkıştırılmış, ekstra bağımlılık) mı?
3. Kayıtların saklama süresi / disk yönetimi politikası var mı (örn. 90 gün
   sonra otomatik silme)?
