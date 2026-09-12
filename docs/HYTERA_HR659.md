# Hytera HR659 — Repeater Network Modülü

## Bilinen durum

- HR659 bir DMR repeater'ı; kullanıcı notuna göre repeater UDP üzerinden
  ağa **konuşma (ses), Radio ID, Grup (Talkgroup), Slot, GPS, SDS**
  bilgisini gönderiyor.
- Kullanıcının elinde gerçek bir paket yakalaması, port numarası, ya da
  resmi bir protokol dokümanı **yok** ("elimde paket yok") - hem
  `D:\Projects\Biem_SDR` (ayrı Python hattı) hem de bu repo bunu teyit etti;
  o repo de Hytera repeater entegrasyonunu açıkça kapsam dışı bırakıp bu
  repoya (C++/Qt) havale ediyor.
- Kullanıcı "yapalım" dedi - yani gerçek veri gelmeden, açık kaynak
  araştırmasına dayanan en iyi tahminle ilerleme kararı kullanıcıya aittir,
  bu doküman ve kod bunu şeffaf şekilde yansıtıyor.

## Açık kaynak araştırması (bu oturumda yapıldı) - CONFIRMED vs ASSUMED

WebSearch/WebFetch ile bulunan, HR659'a özel DEĞİL ama Hytera'nın DMR
repeater ailesinin genel ağ protokolüne dair gerçek, atıf verilebilir
kaynaklar:

- **[OpenIPSC](https://github.com/gopher2/OpenIPSC/blob/master/README.hytera.md)**
  (açık kaynak, Hytera'nın "IP Multi Site Connect" protokolünü tersine
  mühendislikle belgeliyor) - **CONFIRMED**: üç UDP paket tipi var
  (Networking, Service, RDAC), hepsi UDP, her biri kendi portunda. Service
  paketi şunları taşıyor: kaynak Radio ID (24-bit), hedef Grup ID (24-bit),
  slot belirteci ("1111"/"2222"), çağrı tipi (Private/Group Call), DMR sync
  (6 byte), ve 2x AMBE+2 ses segmenti (13.5 byte'lık). Paket tipi byte
  değerleri: 0x01 (voice frame), 0x02 (sync frame), 0x03 (end transmission).
  Status paketleri boşta/aktifken her 60 saniyede bir gönderiliyor.
- **[Hytera_Homebrew_Bridge](https://github.com/OK-DMR/Hytera_Homebrew_Bridge)**
  (Hytera repeater 8.x/9.x ↔ MMDVM/Homebrew köprüsü) - IPSC IP+port
  (P2P, DMR, RDAC) gerektiğini doğruluyor, Kaitai Struct protokol
  tanımlarına referans veriyor ama bu oturumda tam port numaralarına ya da
  Kaitai dosyasının kendisine erişilemedi.
- **[HytBridge](https://github.com/dk7lst/HytBridge)** - "IP-dispatch
  UDP/IP network interface" ile decompressed PCM veri okuyor (IPSC değil,
  muhtemelen tek-repeater/dispatch-console'a daha yakın bir protokol -
  HR659'un durumuna daha yakın olabilir). `doc/HytIPDispatch-Protokoll.odt`
  adında gerçek bir protokol dokümanı içeriyor ama .odt formatı bu
  oturumda okunamadı (WebFetch HTML sayfası okuyor, ofis dosyası değil) -
  **ileride indirilip incelenebilir**.
- Genel arama sonuçları (Hytera Smart Dispatch port listeleri, BrandMeister
  ile ilgili sayfalar - kendisi bu oturumda erişilemedi) IP Multi-Site
  Connect için sıkça anılan UDP port adayları: Master/Networking 50000,
  Service 50001, RDAC 50002. **Bunlar HR659'a özgü olarak doğrulanmadı** -
  genel Hytera repeater ailesi dokümantasyonundan.

**ASSUMED (bizim tahminimiz, hiçbir kaynakta yok)**: yukarıdaki alanların
paket içindeki tam byte OFSETLERİ. OpenIPSC alan varlığını/tipini/boyutunu
belgeliyor, byte-byte bir struct vermiyor. `src/net/hytera/HyteraHR659Source.cpp`
şu an şu yerleşimi varsayıyor (tek yerde toplandı, `kOffset*` sabitleri):

```
offset 0    : paket tipi (1 byte)              - konum varsayım, degerler CONFIRMED
offset 1-3  : kaynak Radio ID (24-bit LE)      - varlik/tip CONFIRMED, konum ASSUMED
offset 4-6  : hedef Grup/Radio ID (24-bit LE)  - varlik/tip CONFIRMED, konum ASSUMED
offset 7    : cagri tipi (1 byte)              - varlik CONFIRMED, encoding + konum ASSUMED
offset 8-9  : slot belirteci (16-bit LE)       - varlik CONFIRMED, encoding + konum ASSUMED
```

Slot belirteci 0x1111 = Slot 1, 0x2222 = Slot 2 olarak yorumlanıyor (OpenIPSC'nin
"1111"/"2222" ifadesinin en dogal okunuşu) - başka bir değer görülürse slot
alanı boş bırakılıyor, hatalı tahmin edilmiyor.

## Bu repodaki mevcut durum

- `src/net/INetworkIngestSource.h`: ortak arayüz - değişmedi.
- `src/net/UdpRawLogger.*`: **çalışan** genel amaçlı yakalama aracı - değişmedi.
- `src/net/hytera/HyteraHR659Source.*`: **artık stub değil** - yukarıdaki
  varsayımsal yerleşime göre paket ayrıştırıyor, `CallStartCallback`/
  `CallEndCallback`'i tetikliyor (ses her zaman `voiceDecoded=false` -
  AMBE+2 decode edilmiyor, ham payload konumu da bilinmediği için henüz
  ayrıca saklanmıyor). Her paket, ayrıştırma başarılı olsun olmasın,
  `[hytera-hr659]` ile stderr'e tek satır tanılama basıyor (tip + uzunluk +
  çıkarılan alanlar) - gerçek trafikte offsetleri düzeltmenin en hızlı yolu
  bu satırları gözlemlemek.
- `src/cli/biem_cli.cpp`: yeni `hytera-live <port> <db_yolu> <kayit_klasoru>
  <kanal_etiketi>` komutu - `HyteraHR659Source`'u gerçek bir `CallRecorder`'a
  bağlayıp uçtan uca çalıştırıyor (arama/DB dahil).
- `tests/test_hytera_hr659.cpp`: **sentetik** paketlerle (yukarıdaki
  varsayımsal yerleşime göre elle inşa edilmiş) parser'ın kendi içinde
  tutarlı olduğunu, çökmediğini (kısa/bilinmeyen/boş paketlerde de) ve
  private/group call ile slot 1/2 ayrımını doğru yaptığını doğruluyor.
  **Bu, varsayılan yerleşimin GERÇEK HR659 trafiğiyle eşleştiğini KANITLAMAZ**
  - sadece kodun kendi varsayımıyla tutarlı olduğunu kanıtlar.
- Gerçek bir UDP soketi üzerinden (Python'dan) uçtan uca da test edildi:
  paket → parse → CallRecorder → SQLite → `search` → doğru sonuç, ham
  hexlog/raw dosyaları da byte-byte doğrulandı.

## Sizden ihtiyacımız olan bilgi (öncelik sırasına göre, herhangi biri yeterli)

1. **En hızlı/en değerli**: repeater'ın IP'sine erişebildiğiniz an,
   `biem_cli hytera-live <tahmini_port> <db> <kayit_klasoru> <etiket>`
   çalıştırıp gerçek bir konuşma/GPS/SDS olayı sırasında ekrana basılan
   `[hytera-hr659]` satırlarını (ve `<kayit_klasoru>/hytera_raw/*.hexlog`
   dosyasını) paylaşın - port bilinmiyorsa önce `udp-capture` ile birkaç
   olası portu (50000/50001/50002 ve repeater'ın kendi ayarlarında görünen
   varsa) deneyin.
2. Hytera'nın bu repeater için yayınladığı resmi bir protokol/SDK dokümanı.
3. `dk7lst/HytBridge` reposundaki `doc/HytIPDispatch-Protokoll.odt` dosyasını
   indirip paylaşabilirseniz (bu oturumda .odt içeriği okunamadı) doğrudan
   faydalı olabilir.

## Bu bilgi gelince yapılacaklar

- `kOffset*` sabitlerini (`HyteraHR659Source.cpp`) gerçek konumlarla
  değiştirmek - kod zaten tek yerde toplanmış durumda, bu küçük bir
  düzeltme olacak, yeniden yazım değil.
- Ses payload'ının formatı (ham AMBE+2 mi, zaten PCM/G.711 mi) netleşince
  `WavWriter`'a bağlamak ya da bir decode adımı eklemek.
- GPS/SDS alanları: OpenIPSC'nin özetinde bu iki alan hiç geçmiyor - hangi
  paket tipinde, hangi formatta geldiği tamamen bilinmiyor, şu an KOD YOK
  (tahmin bile edilmedi - kaynaksız tahmin yerine açık TODO bırakıldı).
