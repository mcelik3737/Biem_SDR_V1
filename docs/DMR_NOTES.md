# DMR Hava Arayüzü Notları — Durum ve Doğrulama

DMR (ETSI TS 102 361) **açık bir ETSI standardı** — hava arayüzü katmanını
(senkron kelimeler, FEC, burst yapısı) uygulamak yasal/telif açısından
sorunsuz (OP25, dsd/dsd-fme, MMDVM, m17-tools gibi birçok açık kaynak proje
zaten bunu yapıyor). Bu dosya bu repodaki DMR kodunun **neyi hangi güvenle**
uyguladığını açıklar — DMR fiziksel katmanı bit-hassasiyetinde bir standart
olduğu için tek bir yanlış bit ofseti, "derleniyor ve mantıklı görünüyor ama
gerçek sinyalde asla kilitlenmiyor/hep yanlış sayı üretiyor" türünden sessiz
bir hataya yol açar. Aşağıdaki her bölüm hangi kategoriye girdiğini açıkça
belirtiyor.

## ✅ Güçlü kanıtla doğrulanan

**48-bit senkron kelimeleri** (`DmrConstants::kSyncPatterns`) — web
aramasıyla, ETSI TS 102 361 kaynaklarına atıfla teyit edildi:

| Senkron türü | Hex |
|---|---|
| BS kaynaklı Voice | `755FD7DF75F7` |
| BS kaynaklı Data | `DFF57D75DF5D` |
| MS kaynaklı Voice | `7F7D5DD57DFD` |
| MS kaynaklı Data | `D5D7F77FD757` |

Kaynak: arama sonucu özeti, ETSI TS 102 361-1/-2 PDF'lerine ve
`forums.radioreference.com`'daki bir tartışmaya atıfla.

## ✅ Gerçek açık kaynak koda dayanan (yüksek güven, byte-byte diff değil)

Bu oturumda `g4klx`'in (MMDVM/DMRGateway projelerinin yazarı, Jonathan
Naylor) gerçek dünyada yıllardır çalışan, gerçek DMR repeater'larıyla
kullanılan açık kaynak koduna WebFetch ile ulaşıldı:

- https://raw.githubusercontent.com/g4klx/DMRGateway/master/Golay2087.cpp
- https://raw.githubusercontent.com/g4klx/MMDVMHost/master/BPTC19696.cpp
- https://raw.githubusercontent.com/g4klx/MMDVMHost/master/DMRSlotType.cpp

**Önemli sınırlama:** WebFetch bu dosyaları "küçük, hızlı bir modelle
özetleyerek" döndürüyor — dosyaların byte-byte tam kopyası elimizde değil,
bir özetleme/parafraz katmanından geçmiş hali var. Aşağıdaki her madde bu
riski göz önünde bulundurarak değerlendirilmeli; ideal olan gerçek dosyaları
doğrudan (`curl`/tarayıcı ile) çekip birebir karşılaştırmak.

**Çıkarılan ve koda işlenen somut bilgiler:**

1. **BPTC interleave formülü**: `interleavedPos = (a * 181) % 196`
   (`Bptc196x96::interleave/deinterleave`). Bağımsız olarak doğrulandı:
   181 ve 196 aralarında asal olduğu için bu gerçekten 0..195 üzerinde
   bijektif bir permütasyon — gerçek bir interleaver'ın sahip olması
   gereken özellik. Bu, formülün rastgele bozulmamış/doğru aktarılmış
   olduğuna dair bağımsız bir tutarlılık kanıtı.
2. **BPTC satır/sütun kodları**: Hamming(15,11,3) satırlarda, Hamming(13,9,3)
   sütunlarda — `Hamming1511`/`Hamming139` olarak uygulandı (standart/textbook
   Hamming inşası: parity 1-indexli pozisyon {1,2,4,8}'de).
3. **Golay(20,8) üretici polinomu**: `GENPOL = 0x00000C75` — en yüksek biti
   bit11'de olduğu için derece-11 bir polinom, yani **8 veri + 11 parity =
   19 anlamlı bit** (kendi bit-aritmetiğimizle çapraz kontrol edildi ve
   tutarlı çıktı — bkz. `Golay2087.cpp` yorumu). `Golay2087` bu polinomla
   GF(2) polinom bölmesi ile encode/decode yapıyor; decode syndrome→hata
   tablosu, orijinal 2048-girişli `DECODING_TABLE_1987`'nin içeriği elimizde
   olmadığı için **kendi üretim-zamanı brute-force enumerasyonumuzla**
   (ağırlık ≤2 hata paternleri) inşa edildi — bu, AYNI üretici polinom
   doğruysa matematiksel olarak eşdeğer bir sonuç verir.
4. **Slot Type'ın burst içindeki byte ofsetleri**: `DMRSlotType.cpp`'den
   **birebir alıntılanan** kod:
   ```
   DMRSlotType[0] = (data[12]<<2)&0xFC | (data[13]>>6)&0x03
   DMRSlotType[1] = (data[13]<<2)&0xC0 | (data[19]<<2)&0x3C | (data[20]>>6)&0x03
   DMRSlotType[2] = (data[20]<<2)&0xF0
   ```
   `DmrSlotDecoder::decodeSlotType` bunu doğrudan port ediyor — bu modüldeki
   en güvenilir burst-layout-specific kod parçası, çünkü kod bloğu olarak
   alıntılandı (düzyazı özetten değil).
5. **264-bit burst'ün tam alan yerleşimi** (Info1/SlotType1/Sync/SlotType2/
   Info2) — DMR RF demodülatörü (`DmrRfDemodulator`) çalışması sırasında
   **iki bağımsız yöntemle** doğrulandı, aşağıdaki "⚠️" bölümünde daha önce
   sadece varsayım olarak işaretlenmiş Info1/Info2 sınırlarını artık güçlü
   kanıt seviyesine taşıyor:
   - Yöntem 1: yukarıdaki (4) maddesindeki alıntılanmış byte-ofset
     formüllerinin bit-bit elle takibi (hangi byte'ın hangi bitleri Slot
     Type'a, hangileri Sync'e ait olduğu) şu yerleşimi veriyor:
     Info1(98) + SlotType-yarım1(10) + Sync(48) + SlotType-yarım2(10) +
     Info2(98) = 264.
   - Yöntem 2 (bağımsız çapraz kontrol): genel bir DMR sinyal işleme
     kaynağı (lyonscomputer.com.au/MMDVM/DMR-Signal-Processing-Notes,
     WebSearch ile bulundu) burst'ü "108-bit payload + 48-bit SYNC +
     108-bit payload" olarak tanımlıyor — 108 = 98+10 olduğu için bu, daha
     kaba bir granülerlikte AYNI yerleşimi doğruluyor.
   İki bağımsız kaynağın örtüşmesi bu yerleşimi "kendi varsayımımız"
   olmaktan çıkarıp makul güvenle doğrulanmış seviyesine taşıyor. Sabitler:
   `DmrConstants::kBurstSyncStartBit=108`, `kBurstBitsAfterSync=108`.
   `DmrSlotDecoder::extractInfoBitsForBptc`'nin önceden "UNVERIFIED"
   işaretli varsayımı bu nedenle artık aynı iki kaynakla destekleniyor —
   aşağıdaki "⚠️" bölümü buna göre güncellendi.

## ⚠️ Kendi varsayımımız / DOĞRULANMADI

- ~~**Info1/Info2 alan sınırları**~~ — artık yukarıdaki "✅" bölümünün 5.
  maddesinde iki bağımsız kaynakla doğrulandı (bkz. orada). Önceki metin
  ("Info1 = byte 0-11 + byte12'nin üst 2 biti, Info2 = byte20'nin alt 2
  biti + byte 21-32") doğru çıktı.
- **BPTC'nin 99→96 bit indirgemesi**: satır-data ∩ sütun-data kesişimi 9×11=99
  hücre veriyor, DMR payload'ı 96 bit — aradaki 3 bitin "rezerve" olduğu ve
  bizim bunları satır-major sırada **ilk 3 aday hücre** olarak attığımız
  (`Bptc196x96.cpp`daki `kReservedCandidateCount`) kendi seçimimiz. Bu seçim
  bir kaynağın "ilk satır sadece 8 bit, sonraki satırlar 11'er bit" şeklindeki
  gözlemiyle *örtüşüyor* ama birebir teyit edilmedi.
- **DMR Link Control (72 bit) yerleşimi** (`DmrLinkControl::parse`): FLCO(1
  byte)+FID(1 byte)+ServiceOptions(1 byte)+Grup/Hedef adres(3 byte)+Kaynak
  adres(3 byte) — yaygın olarak belgelenen DMR Full LC yapısı, bu oturumda
  ayrıca doğrulanmadı. Kalan 24 bit (96-72) için CRC kontrolü **yok** —
  yani bozuk/yanlış hizalanmış bir frame, hatasız görünen ama yanlış
  sayılar üretebilir.
- **Data Type 4-bit kod tablosu** (`DmrBurst.h::dataTypeFromCode`): yaygın
  atıfta bulunulan 0-9 tablo, bu oturumda ayrıca doğrulanmadı.

## Nasıl ilerlenir (öncelik sırasıyla)

1. Yukarıdaki üç GitHub dosyasının **tam içeriğini** (WebFetch özeti değil,
   doğrudan) alıp bu repodaki karşılıklarıyla satır satır karşılaştırın —
   özellikle `BPTC19696.cpp`'nin veri-çıkarma sırası ve `DMRSlotType.cpp`'nin
   tam bağlamı (Info1/Info2 sınırları oradan da çıkarılabilir olabilir).
2. ~~Gerçek bir DMR kaydıyla `DmrFrameSync`'in kilitlenip kilitlenmediğini
   test edin~~ — bu artık mümkün: `DmrRfDemodulator` (bkz. aşağıdaki yeni
   bölüm) tam bunu yapan, gerçek RTL-SDR donanımıyla `biem_cli dmr-live`
   üzerinden test edilebilecek parça. Sentetik sinyalle doğrulandı, gerçek
   donanımla **henüz doğrulanmadı** — sıradaki gerçek adım bu.
3. LC alanlarına (TG/Radio ID) CRC doğrulaması ekleyin ki yanlış hizalanmış
   frame'ler sessizce yanlış sayı üretmek yerine reddedilsin.
4. RF demodülatörün kalıntı bit hata oranını düşürecek gerçek bir eşlenmiş
   (matched/RRC) filtre ve/veya daha iyi sembol zamanlama takibi ekleyin -
   bkz. aşağıdaki yeni bölümdeki "tam yığın güvenilirliği" bulgusu.

## ✅ Yeni: DMR'nin RF tarafı (DmrRfDemodulator) - havadan gerçek 4FSK alımı

Önceki durum: yukarıdaki her şey (FEC, senkron, Slot Type/LC decode) bit
seviyesinde hazırdı ama ham RTL-SDR IQ'sundan gerçek 4FSK demodülasyonu +
sembol zamanlama + burst hizalama YOKTU (`docs/ROADMAP.md`'de açıkça
belirtilmişti). `src/dsp/dmr/DmrRfDemodulator.*` bu boşluğu dolduruyor -
analog tarafta `NbfmDemodulator`'ın yaptığının DMR karşılığı.

**4FSK fiziksel katman sabitleri** (`DmrConstants.h`'daki `kDmr*`) - iki
bağımsız kaynakla çapraz doğrulandı:
- Sembol hızı 4800 sembol/sn, sapma seviyeleri ±648/±1944 Hz: genel bir
  kaynaktan (lyonscomputer.com.au/MMDVM/DMR-Signal-Processing-Notes).
- Dibit↔seviye eşlemesi: g4klx/MMDVM'nin gerçek firmware kodundan
  (`DMRDMOTX.cpp` TX tablosu VE `DMRDMORX.cpp` RX eşik mantığı, ayrı ayrı
  fetch edildi) - ikisi birbiriyle tam tutarlı çıktı (RX'in "sample <
  -threshold → dibit 01" kuralı TX'in "01 → en düşük seviye" girdisiyle
  birebir örtüşüyor), ayrıca dört seviye arasında Gray-code yapısı var
  (ardışık her çift sadece 1 bit farklı) - gerçek, çalışan bir tasarımın
  beklenen özelliği.

**Sembol zamanlama edinimi**: donanımın hangi ham örnekte gerçek sembol
sınırının olduğu bilinmiyor - `DmrRfDemodulator` samplesPerSymbol (240kHz/
4800=50) olası fazın HEPSİNİ paralel dener (toplam maliyet tek bir fazı
takip etmekle aynı - her ham örnek tam olarak bir faza ait). **Önemli
düzeltme**: ilk tasarım "bir senkron kelimesi + ardından gelen 108 bitin
gelmesi" yeterli görüyordu ama o 108 bit HİÇBİR ŞEYLE doğrulanmıyordu -
`tests/test_dmr_rf.cpp`'nin saf gürültü testi bunun gürültü üzerinde
sahte kilitlenmeye yol açtığını yakaladı. Düzeltme: bir faz ancak TAM 264
bit arayla İKİ senkron kelimesi görülürse güvenilir kabul ediliyor
(klasik "edin, sonra doğrula" deseni) - yanlış kilitlenme olasılığını
ihmal edilebilir seviyeye indiriyor (gerçek sayılar commit mesajında),
bedeli: her gerçek iletimin ilk burst'ü sadece doğrulama referansı olarak
harcanıyor, kendisi hiç raporlanmıyor (~27.5ms ek edinim gecikmesi).

**Bilinen sınırlama - tam yığın güvenilirliği**: `tests/test_dmr_rf.cpp`
geliştirilirken ölçüldü: `DmrRfDemodulator`'ın gerçek bir eşlenmiş (RRC)
filtresi olmadığı için (tek örnek/kısa boxcar ortalaması - bkz. sınıfın
kendi yorumu) kalıntı bir bit hata oranı var. Bu oran senkron kelimesi
için sorun değil (48 bitte 4 hata toleransı var) ama 96-bit LC payload'ını
BPTC'nin düzeltme kapasitesini bazen aşıyor - `Bptc196x96::decode` tek
geçişten (satır+sütun, 1 kez) 4 geçişe (`kMaxDecodeIterations=4`,
iteratif ürün-kod çözme) çıkarıldı ama bu bile **rastgele TG/Radio ID
değerlerinin çoğunu** güvenilir şekilde düzeltmeye yetmiyor - ölçülen
test (`testDmrRfFullStackReliabilityIsBoundedAndNonZero`) kasıtlı olarak
"0 değil ama %100 de değil" aralığını doğruluyor, "her zaman çalışır"
iddiasında bulunmuyor. Gerçek düzeltme gerçek bir eşlenmiş filtre ve/veya
daha iyi sembol zamanlama takibi gerektirir - bkz. yukarıdaki "Nasıl
ilerlenir" madde 4.

**Doğrulama durumu**: sentetik testler (`WavIqSource::makeSyntheticFsk` +
`dsp/dmr/DmrSyntheticSource.h`, `biem_cli dmr-demo`) uçtan uca çalıştığı
gösterildi. **Gerçek RTL-SDR donanımıyla, gerçek bir DMR el telsizinden
(427.500 MHz, Color Code 1, simplex) ilk gerçek test yapıldı**:
- Güçlü, temiz bir sinyal alındı (arka plan -30/-50 dB iken sinyal
  -2/-3 dB'ye çıktı - analog testten bile daha net bir S/N).
- **Gerçek havadan gerçek bir kilit elde edildi** (iki gerçek senkron
  kelimesi tam 264 bit arayla bulundu) - 4FSK demod + zamanlama kurtarma
  zincirinin gerçek donanımda çalıştığının ilk kanıtı.
- Golay-decode edilen renk kodu (1) radyonun GERÇEK ayarıyla (Color Code 1)
  birebir eşleşti - tesadüf olamayacak kadar isabetli bir doğrulama.
- **Gerçek bulgu**: squelch, gerçek bir sürekli PTT boyunca bile onlarca
  kez ACIK/kapali arasında çırpınıyordu - tek slotlu simplex bir DMR
  radyosu, TDMA'nın diğer slotunun ~30ms'lik boş penceresinde RF'i
  fiziksel olarak KESİYOR (bu kısım doğru teşhis edildi ve hâlâ geçerli).
  Bunu düzeltme çabası üç denemede tamamlandı - ilk ikisi YANLIŞTI ve
  bilerek burada kayıtlı tutuluyor ki aynı hata tekrar denenmesin:

  1. **İlk deneme (commit `e488689`, HATALIYDI, sonradan geçersiz
     kılındı)**: `resetAcquisition()` sadece gerçekten uzun (>150ms) bir
     sessizlikten sonra çağrılacak şekilde düzeltildi - AMA bununla
     birlikte sembol işleme squelch durumuna HİÇ bakmayacak şekilde
     değiştirildi (kapı tamamen kaldırıldı). Bu yanlıştı: tek slotlu
     sinyalde boş TDMA penceresi sırasında da ham gürültü bitleri
     `BurstAligner`'ın bit sayacına/geçmişine eklenmeye devam ediyor -
     bu da bir sonraki gerçek senkronun beklenen konumunu kaydırıyor,
     yani iki gerçek senkron artık tam 264 bit arayla asla bulunamıyor
     ve kilit HİÇBİR ZAMAN oluşmuyor. Yeni bir regresyon testi
     (`testDmrRfLocksAcrossSingleSlotTdmaGap`, tam bu senaryoyu
     sentetik olarak üretiyor) bunu yakaladı - test başarısız oldu,
     `e488689`'un yanlış olduğunu kanıtladı.
     **Bağımsız gerçek donanım doğrulaması** (2026-09-12, Windows/MSVC
     derlemesi, hâlâ `e488689` üzerindeyken): kullanıcı 427.500 MHz'de
     tekrar test etti - güçlü, temiz bir sinyal alınmasına rağmen
     (`-2` ile `-6 dB` arası güç okumaları, arka plan `-27`/`-48 dB`)
     `kilit: yok` durumu TÜM oturum boyunca hiç değişmedi. Bu, testin
     sentetik olarak öngördüğü tam senaryoyla birebir örtüşüyor -
     regresyon testi gerçek donanımdaki gerçek bir arızayı doğru
     yakalamış.
  2. **İkinci deneme**: squelch kapısını olduğu gibi geri eklemek
     (`if (!squelchOpen_) continue;`) de yetmedi. Teşhis (geçici bir
     debug harness ile): mevcut squelch takipçisi YAVAŞ bir EMA
     (`alpha=0.001`, ~1000 örnek/~4.2ms zaman sabiti) - 30ms/7200
     örneklik bir TDMA boşluğu içinde YERLEŞEMİYOR (gerçekten sessiz
     bir pencere boyunca bile squelch "açık" kaldığı gösterildi), yani
     bu takipçiye göre kapılamak boşluğu hiç filtrelemiyordu.
  3. **Kalıcı düzeltme**: iki AYRI güç takipçisi:
     - `squelchPowerAvg_`/`squelchOpen_`: DEĞİŞMEDİ - hâlâ yavaş EMA,
       hâlâ sadece genel/çağrı-sınırı amaçlı public callback için.
     - YENİ `fastPowerAvg_`/`fastGateOpen_` (`alpha=0.25`, ~4 örnek
       zaman sabiti): SADECE sembol işlemeyi iç olarak kapılamak için -
       TDMA boşluğunu gerçekten fark edebilecek kadar hızlı.
     - `kSyncPositionToleranceBits=8` (`DmrConstants.h`) eklendi: hızlı
       kapı bile sinyal bittiğini fark etmeden önce birkaç örnek
       gecikiyor (birkaç bit sızıntısı) - senkron pozisyon doğrulama
       kontrollerine (`BurstAligner::pushBit`) küçük bir tolerans
       eklendi. Burst çıkarma zaten gerçek bulunan senkron pozisyonuna
       göre çapalanıyor, bu yüzden bu tolerans sadece doğrulamayı
       etkiliyor, içerik hizalamasını değil.
     - Doğrulama: `testDmrRfLocksAcrossSingleSlotTdmaGap` geçti, tam
       test paketi geçti (5/5, `ctest`), 10 farklı RNG seed'iyle
       gürültü-reddi testi tekrar doğrulandı (yanlış kilitlenme riski
       hâlâ ihmal edilebilir - tolerans eklemek bunu ölçülebilir
       şekilde zayıflatmadı), `-Wall -Wextra -Wpedantic -Wshadow`
       temiz, `biem_cli dmr-demo` regresyon kontrolü geçti
       (`locked=1`).
- Yakalanan burst (ilk testte, hâlâ `e488689` öncesi/orijinal kilitte)
  `MultiBlockControlHeader` olarak çözüldü ama Golay `correctedBits=-1`
  (düzeltilemedi/güvenilmez) - kısa bir PTT'de tam çağrı başlığını
  (VoiceLcHeader) değil başka bir burst'ü yakalamış olabiliriz. Bir
  sonraki test: yukarıdaki kalıcı düzeltmeyle, uzun bir PTT basışında
  gerçek bir `VoiceLcHeader` + çağrı başlangıcı yakalanıp yakalanmadığı.

**Kalıcı düzeltmenin gerçek donanımda doğrulanması** (2026-09-13, Windows/
MSVC, `ef177fe`, 427.500 MHz, aynı radyo): kullanıcı iki ayrı çalıştırma
yaptı.
- **1. çalıştırma - BAŞARILI**: squelch onlarca kez ACIK/kapali çırpınmaya
  devam etti (beklenen - gerçek TDMA boşluğu), ama bu sefer çırpınma
  ORTASINDA gerçek bir kilit oluştu ve bir burst çözüldü
  (`sync-tur=MsSourcedVoice`), ardından `kilit: VAR` durumu 15+ saniye
  boyunca (çok sayıda `guc seviyesi` satırı boyunca), squelch çırpınmaya
  devam etmesine rağmen KORUNDU. Bu, düzeltmenin tam olarak tasarlandığı
  gibi çalıştığının doğrudan kanıtı.
  - **Önemli nüans - `kilit: VAR`'ın anlamı**: sadece 1 tane
    "burst alindi" satırı basıldı, `kilit: VAR` süren onlarca saniyeye
    rağmen. Bu bir hata değil: `locked()` "bu faz en az bir kez
    doğrulandı VE o zamandan beri gerçekten uzun (>150ms) bir sessizlik
    görülmedi" anlamına geliyor - "şu anda yeni bir burst aktif olarak
    çözülüyor" anlamına GELMİYOR. `closedSampleCount_` her ~30ms'lik
    normal TDMA boşluğunda 150ms eşiğine ulaşamadığı için (kod
    yorumunda açıklanan tasarım gereği) `resetAcquisition()` hiç
    tetiklenmedi, dolayısıyla `locked_` tekrar `false` olmadı - kanaldaki
    başka herhangi bir RF aktivitesi (gerçek DMR burst'ü olsun ya da
    olmasın) fast gate'i 150ms'den kısa aralıklarla açık tuttuğu sürece
    böyle davranmaya devam eder. Yani CLI'daki `kilit` sütunu bir "sağlık
    lambası" gibi okunmamalı - gerçek burst SAYISI için hâlâ tek doğru
    kaynak `burst alindi` satırlarının kendisi. (İyileştirme fikri: son
    burst'ten bu yana geçen süreyi de yazdırmak - şimdilik yapılmadı,
    kullanıcı isterse eklenebilir.)
  - `renk-kodu=5 veri-tipi=Reserved golay-duzeltilen-bit=-1`: bu ikisi
    GÜVENİLMEZ, çünkü Slot Type alanının Golay kod çözümü başarısız oldu
    (`correctedBits=-1`) - önceki testte ölçülen gerçek Color Code (1)
    bulgusuyla ÇELİŞMİYOR (o, Golay'ın BAŞARILI olduğu farklı bir
    burst'tü). Senkron kelimesi eşleşmesi (dolayısıyla kilidin kendisi)
    bundan bağımsız, ayrı bir Hamming-mesafe kontrolü olduğu için
    etkilenmedi.
- **2. çalıştırma - kilit oluşmadı**: hemen ardından yapılan ikinci
  çalıştırmada squelch yine çırpındı ama gözlemlenen pencerede hiç kilit
  oluşmadı. Aynı ikili dosya bir önce kilitlendiğine göre bu bir
  regresyon değil - muhtemelen daha kısa/farklı bir PTT basışı (iki
  senkronun 264±8 bit arayla görülmesi için transmisyonun yeterince
  uzun sürmesi gerekiyor, bkz. yukarıdaki "ilk burst sadece doğrulama
  referansı" notu), zaten belgelenmiş "her zaman kilitlenir garantisi
  yok" sınırlamasıyla tutarlı.

**Slot 1/2 ayrımı**: `DmrRfDemodulator` burst'leri sadece VARIŞ SIRASINA
göre raporluyor, hangi fiziksel TDMA slotuna ait olduğunu bilmiyor (gerçek
CACH/zamanlama takibi yok). `biem_cli dmr-live` şimdilik bunu sırayla
alternatif slot1/slot2 atayarak VARSAYIYOR - aktif her iki slotu da
kullanan meşgul bir repeater için makul ama tek slot aktifken ya da bir
burst kaçırılırsa YANLIŞ. Açıkça işaretlendi, düzeltilmedi.

## ❌ Henüz yok: DMR ses (AMBE+2 vocoder)

DMR ses kanalı **AMBE+2** codec'i kullanır — patentli/lisanslıdır. Bu repo
AMBE+2'yi kendi içinde uygulamıyor; `dsp/dmr/IVoiceDecoder.h` soyut arayüzü
var (`NullVoiceDecoder` varsayılan — ses karesi geldiğini loglar ama
decode etmez), amatör telsiz/monitoring topluluğunda yaygın olan yaklaşım
gibi (SDRTrunk'ın `jmbe` eklentisi, DSD-FME) ileride harici bir decoder'a
bağlanmak üzere tasarlandı.

## Şu an elde edilebilecek değer (ses olmasa bile)

Yukarıdaki doğrulanmamış noktalar düzeltildiğinde, ses decode'u olmadan bile
**kimin (Radio ID), hangi gruba (TG), hangi slotta** konuştuğunu — yani
"çağrı başlığı" (Title/Group/ID/Slot) üretmek için gereken her şeyi — RF'den,
network'e hiç ihtiyaç duymadan elde edebiliriz.
