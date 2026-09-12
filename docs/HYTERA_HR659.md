# Hytera HR659 — Repeater Network Modülü

## Bilinen durum

- HR659 bir DMR repeater'ı; kullanıcı notuna göre repeater UDP üzerinden
  ağa **konuşma (ses), Radio ID, Grup (Talkgroup), Slot, GPS, SDS**
  bilgisini gönderiyor.
- Tam paket formatı, port numaraları ve encapsulation (Hytera'nın kendi
  proprietary protokolü mü, yoksa DMR-MARC/IPSC/Hytera IP Multi-Site
  Connect türevi bir şey mi) **henüz bilinmiyor** — kullanıcı gerçek
  port bilgisini iletecek.

## Bu repodaki mevcut durum

- `src/net/INetworkIngestSource.h`: Her repeater/network kaynağının
  uygulaması gereken ortak arayüz (start/stop, `CallRecord` + varsa PCM
  ses üretir) — SDR yolundan tamamen bağımsız, aynı `Database`'e akar.
- `src/net/UdpRawLogger.*`: **Şimdiden çalışan** genel amaçlı bir araç —
  verilen host:port'u dinler, gelen her UDP paketini zaman damgasıyla hem
  hex-dump hem de ham binary olarak diske yazar. Bunu HR659'un IP'sine ve
  (tahmini/bilinen) portlarına karşı çalıştırıp gerçek trafiği yakalamak,
  protokolü tersine çıkarmanın (ya da Hytera'nın resmi SDK/protokol
  dokümanıyla karşılaştırmanın) en hızlı yolu.
- `src/net/hytera/HyteraHR659Source.*`: **Stub.** Şu an hiçbir şeyi
  parse etmiyor; sadece arayüzü uyguluyor ve yapılandırılan
  host:port'tan `UdpRawLogger` mantığıyla veri okuyup logluyor. Gerçek
  paket formatı geldiğinde bu sınıfın içini dolduracağız.

## Sizden ihtiyacımız olan bilgi (bir sonraki adım)

1. **UDP port numarası/numaraları** (ses, sinyalleşme/metadata, GPS/SDS
   ayrı portlarda mı gidiyor, tek portta mı karışık?).
2. Mümkünse `UdpRawLogger` ile alınmış **gerçek bir paket yakalaması**
   (ya da Wireshark ile alınmış bir `.pcap`) — en az birkaç saniyelik bir
   konuşma + bir GPS/SDS paketi içeren bir örnek yeterli.
3. Hytera'nın bu repeater için yayınladığı resmi bir protokol/SDK dokümanı
   varsa (ör. "Hytera IP Site Connect Protocol" ya da dispatch-console
   entegrasyon kılavuzu) — varsa parse'ı doğrudan doğru yazmamızı sağlar,
   yoksa `UdpRawLogger` çıktısından tersine çıkarımla ilerleriz.

## Bu bilgi gelince yapılacaklar

- `HyteraHR659Source::onPacket()` içine gerçek ayrıştırmayı yazmak: paketten
  Radio ID / TG(Grup) / Slot / GPS / SDS alanlarını çıkarıp bir `CallRecord`
  doldurmak.
- Ses payload'ının formatı (ham AMBE mi, zaten PCM/G.711 mi, yoksa
  Hytera'ya özgü bir codec mi) belirlenince `WavWriter`'a doğrudan
  bağlamak ya da gerekiyorsa bir decode adımı eklemek.
