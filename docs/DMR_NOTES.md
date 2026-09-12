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

## ⚠️ Kendi varsayımımız / DOĞRULANMADI

- **Info1/Info2 alan sınırları** (`DmrSlotDecoder::extractInfoBitsForBptc`):
  Slot Type'ın byte 12-13 ve 19-20'yi kullandığından yola çıkarak "Info1 =
  byte 0-11 + byte12'nin üst 2 biti (98 bit), Info2 = byte20'nin alt 2 biti +
  byte 21-32 (98 bit)" varsayıldı. Bu, gördüğümüz somut byte ofsetleriyle
  *tutarlı* ama bağımsız olarak *doğrulanmadı*.
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
2. Gerçek bir DMR kaydıyla (SDR ile alınmış ham I/Q, ya da bir DMR
   repeater'ından log) `DmrFrameSync`'in kilitlenip kilitlenmediğini,
   ardından `DmrSlotDecoder::decodeSlotType`'ın makul (Golay `correctedBits
   != -1`) sonuçlar üretip üretmediğini test edin.
3. LC alanlarına (TG/Radio ID) CRC doğrulaması ekleyin ki yanlış hizalanmış
   frame'ler sessizce yanlış sayı üretmek yerine reddedilsin.

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
