# Mimari

## Veri akışı (tek ortak boru hattı)

```
                    ┌─────────────────────┐
  RTL-SDR USB  ───▶ │ IqSource             │
  (RtlSdrSource)    │ (soyut arayüz)       │
                    └─────────┬────────────┘
                              │ I/Q örnekler
                    ┌─────────▼────────────┐        ┌──────────────────────┐
                    │ NbfmDemodulator       │        │ DmrFrameSync +        │
                    │ (analog FM)           │        │ DmrSlotDecoder +      │
                    │                       │        │ DmrLinkControl        │
                    └─────────┬────────────┘        └──────────┬────────────┘
                              │ PCM ses                          │ CallMeta (TG/ID/Slot/CC)
                              │                                  │ + PCM (voice decoder varsa)
                              ▼                                  ▼
                    ┌──────────────────────────────────────────────┐
                    │              CallRecorder                     │
                    │  (squelch/voice-activity -> çağrı başlangıç/  │
                    │   bitiş; PCM'i WavWriter ile dosyaya yazar)   │
                    └───────────────────────┬────────────────────────┘
                                             │ CallRecord (id, ts, süre,
                                             │ freq, mod, group/tg, radioId,
                                             │ slot, gps, sdsText, title,
                                             │ audioPath, source)
                                             ▼
                                     ┌───────────────┐
  Repeater (UDP)  ──▶ HyteraHR659Source ──▶  Database   │◀── CallLogWidget
  (net/hytera/)       (net/)            │  (SQLite)     │    (arama/filtre/
                                        └───────────────┘     dinleme)
```

Hem SDR/RF yolundan hem de network/repeater yolundan gelen çağrılar **aynı
`CallRecord` modeline** ve **aynı `Database`/arama arayüzüne** düşer — arama
ekranı kaynağın SDR mi yoksa repeater mi olduğunu önemsemez, sadece
tarih/başlık/ID/grup/slot bazında filtreler.

## Modül sınırları ve bağımlılıklar

| Katman | Bağımlılık | Neden ayrı |
|---|---|---|
| `core/` | sadece SQLite3 + std | Platformdan bağımsız iş mantığı; Qt/SoapySDR olmadan da test edilebilir |
| `dsp/` (NBFM, WavIqSource) | sadece std | Saf DSP matematiği — sentetik sinyalle sandbox'ta bile test edilebilir |
| `dsp/` (RtlSdrSource) | librtlsdr (opsiyonel) | Gerçek donanım yoksa CMake bu hedefi otomatik atlar |
| `dsp/dmr/` | sadece std | FEC/frame-sync algoritmaları donanımdan bağımsız, ayrı test edilebilir |
| `net/` | POSIX/BSD soket (Windows'ta Winsock2) | Repeater entegrasyonu SDR'dan tamamen bağımsız bir giriş yolu |
| `ui/` | Qt6 (opsiyonel) | Yoksa `biem_cli` ile aynı çekirdek işlevler headless çalışır |

## Neden bu ayrım?

Kullanıcının isteği iki farklı sinyal kaynağını (SDR/RF ve repeater/network)
aynı çağrı kaydı ürününde birleştirmek. Bu ikisi fiziksel olarak çok
farklı şeyler (I/Q örnekleme + DSP demodülasyon vs. UDP paket ayrıştırma)
olduğu için `IqSource`/`INetworkIngestSource` ayrımı kalıcı: yeni bir SDR
donanımı (ör. Airspy, HackRF) veya yeni bir repeater markası eklemek,
sadece o arayüzü uygulayan yeni bir sınıf eklemek anlamına gelir — geri
kalan boru hattı (demod/decode hariç) değişmez.

## Genişletme noktaları

- **Yeni SDR donanımı**: `dsp/IqSource.h` arayüzünü uygulayan yeni bir sınıf.
- **Yeni repeater markası/modeli**: `net/INetworkIngestSource.h` arayüzünü
  uygulayan yeni bir sınıf (bkz. `net/hytera/HyteraHR659Source` şablonu).
- **DMR ses (AMBE+2) decode**: `dsp/dmr/IVoiceDecoder.h` arayüzü — bkz.
  `docs/DMR_NOTES.md`.
