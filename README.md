# Slice75 HE Native

**Türkçe** · [English](#english)

Chilkey Slice75 HE manyetik (Hall effect) klavye için **resmi olmayan**, hafif bir Windows yapılandırma
programı. Üreticinin tarayıcı tabanlı sürücü sayfasının yerine, Chrome açmadan çalışan tek bir `.exe`.

> Bu proje Chilkey ile bağlantılı değildir ve onlar tarafından desteklenmez. Kendi sorumluluğunuzda kullanın.

![Işık ayarları](docs/screenshot-lighting.png)

## Neden?

| | Tarayıcıda sürücü sayfası | Slice75 HE Native |
|---|---|---|
| RAM | birkaç yüz MB (Chrome) | ~50 MB |
| Boştayken CPU | – | ~%0 (yalnızca değişiklik olunca çizer) |
| Kurulum | Chrome/Edge + internet | tek `.exe`, internet gerekmez |
| Dil | – | Türkçe, English, Deutsch, 中文, 日本語 |

C++, Win32, Direct3D 11 ve [Dear ImGui](https://github.com/ocornut/imgui) ile yazıldı. Klavyeyle doğrudan
Windows HID API üzerinden konuşur.

## Özellikler

- **Tuş atama**: 4 katman (Fn0–Fn3); harf, sembol, medya, fare ve sistem tuşları
- **Performans**: genel ya da tuş bazlı basma noktası, Rapid Trigger, ölü bölgeler, switch tipi,
  Super Response, üst ölü bölge, canlı tuş derinliği testi
- **Gelişmiş tuşlar**: DKS, MPT, Mod-Tap, Toggle, END, RS, SOCD (Snap Tap)
- **Makrolar**: klavyeden kayıt veya elle düzenleme, 4 çalışma modu
- **Işık**: statik renkler, 20 efekt, tuş bazlı renk (ör. sadece WASD farklı), parlaklık, hız, uyku
- **Cihaz**: polling rate (8 kHz'e kadar), Windows/macOS modu, 4 profil, kalibrasyon, fabrika ayarı

Firmware güncellemesi ve ekran görseli yükleme **yoktur**; bunlar için üreticinin sayfasını kullanın.

![Performans](docs/screenshot-performance.png)

## İndirme ve kullanım

1. [Releases](../../releases/latest) sayfasından `Slice75HE.exe`'yi indirin ve çalıştırın. Kurulum gerekmez
   (Windows 10/11).
2. Klavyeyi USB kablosuyla bağlayın; program otomatik bulur.
3. Tarayıcıda üreticinin sürücü sayfası açıksa kapatın; aynı anda yalnızca bir program klavyeyle konuşmalı.
4. Dili sağ üstteki menüden değiştirebilirsiniz.

Makrolar klavyeden geri okunamadığı için `%APPDATA%\Slice75HE\macros.txt` dosyasında saklanır.

### "Windows bilgisayarınızı korudu" uyarısı

Program dijital olarak imzalı olmadığı için Windows SmartScreen ilk açılışta bu uyarıyı gösterebilir.
**Ek bilgi → Yine de çalıştır** ile açabilirsiniz. Program internete bağlanmaz ve hiçbir şey kurmaz.

Dosyayı kendiniz kontrol etmek isterseniz:

- **VirusTotal taraması:** [Slice75HE.exe v0.2.0](https://www.virustotal.com/gui/file/aa32cce4806809edcfe262522697791c20931aaecaabf639e72a61b4735a11ea)
- **SHA-256:** `aa32cce4806809edcfe262522697791c20931aaecaabf639e72a61b4735a11ea`
  (PowerShell: `Get-FileHash Slice75HE.exe`)
- Kaynak kodun tamamı bu repoda; exe her sürümde GitHub Actions ile de derlenir.

## Durum

Test edilen cihaz: Slice75 HE, firmware App V1.1.7, protokol 1.0.7.

Tüm yazma işlemleri gerçek klavyede otomatik testle doğrulandı (yaz → klavyeden geri oku → eski değeri
geri yükle): tuş atama, basma noktası, Rapid Trigger, ölü bölgeler, DKS, MPT, Mod-Tap, Toggle, END, RS, SOCD,
makro, tuş bazlı renk, canlı tuş derinliği. Ayrıntılar: [docs/PROTOCOL.md](docs/PROTOCOL.md),
değişiklikler: [CHANGELOG.md](CHANGELOG.md).

Kendi klavyenizde testi çalıştırmak için: `Slice75HE.exe --selftest selftest.log`

## Katkıda bulunma

Hata bildirimleri, çeviriler ve kod katkıları memnuniyetle karşılanır. Derleme, kod yapısı ve test için
[CONTRIBUTING.md](CONTRIBUTING.md) dosyasına bakın.

```powershell
.\build.ps1          # bin\Slice75HE.exe (Visual Studio 2022 Build Tools gerekir)
```

## Lisans

[MIT](LICENSE). Dear ImGui kendi MIT lisansıyla dağıtılır.

---

## English

Unofficial, lightweight native Windows configurator for the **Chilkey Slice75 HE** Hall-effect keyboard: a
single `.exe` (~50 MB of RAM, ~0% CPU when idle) instead of the vendor's browser-based driver page.
**Not affiliated with Chilkey.**

**Features:** keymap (4 layers) · global / per-key actuation, Rapid Trigger, deadzones, switch type, Super
Response · advanced keys (DKS, MPT, Mod-Tap, Toggle, END, RS, SOCD) · macros (record or edit, 4 modes) · RGB
lighting (static, 20 effects, per-key colours) · polling rate up to 8 kHz, Win/Mac mode, 4 profiles,
calibration · UI in Türkçe, English, Deutsch, 中文, 日本語. Firmware updates are intentionally not included.

**Download:** get `Slice75HE.exe` from [Releases](../../releases/latest) and run it (Windows 10/11, no
installer). If SmartScreen says "Windows protected your PC", click **More info → Run anyway**: the exe is not
code-signed yet. You can check it on
[VirusTotal](https://www.virustotal.com/gui/file/aa32cce4806809edcfe262522697791c20931aaecaabf639e72a61b4735a11ea)
or compare its SHA-256 (`aa32cce4806809edcfe262522697791c20931aaecaabf639e72a61b4735a11ea`).

**Status:** tested on Slice75 HE firmware App V1.1.7 / protocol 1.0.7. Every write operation is verified on
hardware by the built-in self-test (`Slice75HE.exe --selftest selftest.log`). Protocol notes are in
[docs/PROTOCOL.md](docs/PROTOCOL.md).

**Contributing:** see [CONTRIBUTING.md](CONTRIBUTING.md). Bug reports, translations and support for other
boards using the same protocol are very welcome.

**License:** [MIT](LICENSE).
