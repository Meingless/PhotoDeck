# PhotoDeck 0.11.0-ea.1 · Rust

**Rust’a geçtik.** Yeni erken erişimin ana kaynak kodu artık Rust. [İndirmeler](https://github.com/Meingless/PhotoDeck/releases/tag/v0.11.0-ea.2) · [Sürüm duyurusu ve kalan farklar](docs/releases/0.11.0-ea.2.md) · [Kurulum](EARLY-ACCESS.md)

PhotoDeck 0.10.0-ea.4 (`f97ef8b`) temel alınarak yazılmış yerel Rust fotoğraf ve katman düzenleyici. Ana uygulama `src/` altında; varsayılan başlatıcı ve derleme Rust sürümünü kullanır. Tarayıcı, Qt, Python veya ağ servisi çalıştırmaz. C++ kodu yalnızca uyumluluk referansı olarak korunur: [önceki sürümün belgeleri](legacy/qt/README.md).

**0.11.0-ea.2**, **Klasik Pro** arayüzünü, özgün PhotoDeck logosunu ve arka plan işlerini içerir. [Yenilikler ve doğrulama sınırları](docs/IMPROVEMENTS-2026-11-08.md).

## Çalıştırma

Rust 1.89 veya sonrası ve sistem C derleyicisi gerekir. Linux'ta X11/Wayland ile OpenGL kitaplıkları gerekir. Debian/Ubuntu geliştirme paketleri: `build-essential pkg-config libxkbcommon-dev libxkbcommon-x11-0 libwayland-dev libgl1-mesa-dev`. Rust'ı [rustup](https://rustup.rs) ile kurabilirsiniz.

```sh
./scripts/build.sh
./launch.sh
```

Geliştirme ve kontroller:

```sh
cargo run -- --no-settings
cargo fmt --all -- --check
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --all-targets --locked
cargo build --release --locked
```

Windows: `target/release/photodeck.exe`. Linux/macOS: `target/release/photodeck`. `./scripts/install-user.sh` Linux uygulama menüsüne aynı başlatıcıyı ekler. Derlenmiş eski `bin/photodeck` otomatik kullanılmaz.

## Düzenleme

- Çoklu belgeler, dosya sürükleyip bırakma, görüntü panosu, katman listesi ve gezilebilir geri alma geçmişi.
- Piksel, düzenlenebilir yerel-font metni ve gömülü akıllı nesneler; kaynak koruyan dönüşüm, maske, kırpma zincirleri ve 27 harmanlama modu.
- Fırça, kalem, silgi, klon, kova, degrade, şekiller, renk seçici, taşıma, kırpma ve seçim araçları. Özel fırça uçları ve `.pdbrush` kütüphaneleri.
- Dikdörtgen/elips/kement/çokgen/değnek seçimleri; alfa kapsamı, ekleme/çıkarma/kesişim, feather, genişletme/daraltma ve adlandırılmış seçimler.
- Kaynak koruyan tonlar, seviyeler/eğriler, Gaussian, unsharp, high-pass, tekrarlanabilir gren ve pikselleştirme.
- PNG/JPEG/WebP/BMP/TIFF çıktı, GIF ilk-kare girişi, DPI, sRGB/Display P3/Adobe RGB ICC dönüşümü, PDF ve özgünleri koruyan toplu çıktı. WebP çıktı kayıpsızdır.
- Türkçe arayüz, çalışma alanları, görünüm ayarları, `.pdui` içe/dışa aktarımı ve arka plan kurtarma.
- Klasik Pro: iki sütun araçlar, katman/maske küçük resimleri, renk/özellik/geçmiş panelleri, cetveller, F1 rehberi, son dosyalar ve arayüz ölçekleme.
- İlerleme ve iptal destekli arka plan önizleme, görüntü/PDF/toplu çıktı ve hızlı kement seçimi. Tamamlanmış toplu çıktılar iptal edildiğinde korunur.
- Wayland/Windows Ink kalem basıncı ve eğimi bağlantısı; Linux yazdırma portalı, Windows ve macOS yerel yazıcı diyalog adaptörleri. Fiziksel cihaz doğrulaması ve platform sınırları geçiş notlarında belirtilir.

## Dosyalar ve güvenli kayıt

`.pdeck` v1–v7 Qt dosyaları doğrudan Rust içinde okunur. Normal kayıt, akıllı nesne varsa v7, adlandırılmış seçim varsa v6, diğer durumlarda v5 kullanır. Geçici etkin seçim yalnız kurtarma kaydına yazılır. Eski belgenin biçimini yerinde yükseltmek yerine arayüz ayrı kopyaya yönlendirir.

Dosya kaydı aynı klasörde geçici dosya, disk eşitleme ve atomik değiştirme kullanır. Açık belgenin diskteki sürümü değişmiş/silinmişse sessizce üzerine yazılmaz. Kurtarma, tek değişmez anlık görüntü üzerinde arka planda çalışır; oturum kilitleri ve kaydet/at işlemiyle eşzamanlı yayınlama denetimi vardır. Kurtarma, henüz tamamlanmamış hareketi veya disk hatasında bütün değişiklikleri koruma garantisi değildir.

16 megapiksel/16384 kenar, 64 katman, 256 MiB kaynak/dönüşüm ve 30 durum/128 MiB ek geçmiş sınırları korunur. Bu değerler toplam süreç RAM sınırı değildir.

## Geçiş ve doğrulama

[Rust geçiş notları](docs/RUST-MIGRATION.md), özellik sınırlarını ve doğrulama kanıtlarını açıklar. [Rust CI](.github/workflows/rust.yml) Linux, Windows ve iki macOS mimarisi için tanımlıdır; yerel Linux testi diğer platformların çalıştırılmış kanıtı değildir. Eski Qt yayın iş akışları [arşivdedir](legacy/qt/workflows); Rust değişikliği eski EA4 paketini otomatik yayımlamaz.

Kaynak düzeni: `types/document/render` belge ve kompozisyon; `effects/selection/brush/text` düzenleme hesaplamaları; `project/output/recovery/settings` dosya servisleri; `app/main` masaüstü arayüzü. Testler `tests` ve modüllerde bulunur. Uygulama MIT; bağımlılık bilgileri [üçüncü taraf bildirimindedir](packaging/RUST-THIRD-PARTY.md).

`jobs` tek arka plan işçisini, `platform` tablet/yazdırma bağlantılarını yönetir. [Parser fuzz çalışması](fuzz/README.md) ayrı bir workspace'tir. `scripts/native-ui-smoke.py` izole geçici projede işletim sistemi klavye olaylarıyla ekle/geri al/yinele/kaydet akışını, Linux'ta ayrıca fareyle çizimi denetler.
