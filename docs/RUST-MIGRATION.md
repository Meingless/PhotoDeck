# Rust geçişi

Temel: `f97ef8bb95a1376a7b9f7ec1cf131e1ca364a5d9`, PhotoDeck 0.10.0-ea.4. Yerel kaynak ile uzak `main` bu SHA'da eşleşti. Rust geliştirme sürümü `0.11.0-ea.1`; yayımlanmış EA4 paketiyle aynı ikili değildir.

## Mimari

Belge ve görüntü işlemleri GUI'den bağımsız Rust modülleridir. Piksel/maskeler `Arc` ile paylaşılır; yazma `Arc::make_mut` kullanır. Geçmiş içerik kimliği ile geçici seçim kimliğini ayırır. Reddedilen işlemler, bekleyen hareketler, no-op ve undo/redo davranışı modelde sınanır. Akıllı nesne kopyası yeni kaynak kimliği alır; çerçeve, dönüşüm, kaynak ve efektler korunur.

egui/eframe, OpenGL ve yerel pencere sistemiyle çalışır. Yerel metin cosmic-text ile şekillendirilir; eski projedeki metin görüntüsü açıkça düzenlenene kadar yeniden çizilmez. Little CMS yalnız renk dönüşümünü yapar; C++/Qt uygulama kodu çağrılmaz.

`project` Qt QDataStream'in v1–v7 big-endian kayıtlarını doğrudan okur/yazar. String, görüntü, seçim ve toplam bellek sınırları tahsis öncesinde denetlenir. Proje kaydı aynı dizinde geçici dosya ve atomik değiştirme kullanır; SHA-256, dosya kimliği ve yol denetimleri dış değişikliği yakalar. Windows'ta dosya kimliği sistem handle'ından okunur. Kontrol ile atomik değiştirme arasındaki çok dar harici-yazar yarışı tümüyle ortadan kalkmış değildir.

Kurtarma işçisi büyük kodlama/yazma/eşitlemeyi arka planda yapar. Yalnız yayınlama ile kaydet/at iptali ortak kilitle sıralanır. Etkin başka oturumun dosyası alınmaz. Kilidi bulunmayan yeni Qt oturum biçimindeki eski kurtarma kayıtları da önerilir; var olan Qt pid-kilidi Rust tarafından zorla kaldırılmaz.

## Korunan kaynaklar

Önceki C++ kaynakları, testleri, `.pdeck` fixture'ları ve CMake/qmake dosyaları doğrulama referansıdır. Varsayılan `launch.sh` yalnız Rust binary'sini başlatır. Eski build betiği `legacy/qt/scripts/build.sh`, eski README `legacy/qt/README.md`, eski CI/yayın YAML'ları `legacy/qt/workflows` içindedir. Eski sürümün tarihsel test raporları Rust kanıtı sayılmaz.

Kullanıcı ayarları ayrı `PhotoDeck-Rust` uygulama dizinindedir; eski Qt ayar dosyası değiştirilmez. `.pdbrush` ve `.pdui` dosyaları arayüzden içe aktarılabilir. Qt'nin ikili `dockState` verisi aktarımda saklanır; Qt widget yerleşimini Rust içinde çalıştırmaz.

## Doğrulama

Komutlar:

```sh
cargo fmt --all -- --check
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --all-targets --locked
cargo build --release --locked
./launch.sh --no-settings --screenshot /tmp/photodeck-rust.png
```

`tests/fixtures/qt-v5.pdeck`, `qt-v6.pdeck`, `qt-v7.pdeck` ve `qt-v7.png`, yukarıdaki SHA'nın yerel Qt6 kaynak derlemesinden `tests/qt_compat_oracle.cpp` ile üretildi. Legacy v1 ve v2–v5 fixture'ları da okunur. Qt → Rust kayıt → Qt açılış v7 karşılaştırmasında Qt çıktısı **piksel piksel aynı**. Rust renderer'ın aynı örnekte alfa kanalı tam eşleşir; RGB farkı en fazla 5/255'tir. Rust düz RGBA, Qt ara adımlarda 8 bit premultiplied RGBA kullandığından bütün rasterizasyonların piksel özdeşliği iddia edilmez.

Testler; tüm kesme noktalarında parser reddi, geçersiz uzunluklar, gerçek Qt round-trip, kayıt çakışması/silinme/sembolik yol, ayrı kopya yükseltmesi, ICC profilli raster dosyaları, PDF hata koruması, brush kütüphanesi çakışması/bozuk yedeği, kurtarma sahipliği ve çalışan işçi sırasında atma durumlarını kapsar. Çekirdek testleri 27 harmanlama/kırpma alfa davranışını, seçim ve kaynak korumayı, smart nesneleri, filtreleri, fırça aralığını ve geçmişi sınar.

Yerel kanıt Linux içindir. CI dosyasının eklenmesi Windows/macOS testlerinin çalıştırıldığı anlamına gelmez. Son yerel test/derleme günlükleri ve masaüstü görüntüleri `artifacts/rust/` altında tutulur.

## Kalan platform farkları

- Fırça motoru basınç/eğim değerlerini hesaplar ve presetlerde saklar; kullanılan eframe/winit sürümü yerel kalemin basınç, eğim ve silgi ucunu bu arayüze iletmez. Fare tam basınçla çalışır. Bu açıdan Qt tablet iş akışı henüz eşdeğer değildir.
- Yazdırma, üretilen PDF'yi sistem görüntüleyicisine açıp oradan yazdırma yoludur; uygulama içindeki eski Qt yazıcı diyaloğu taşınmadı. Fiziksel yazıcı/tablet testi yapılmadı.
- WebP kayıpsız kodlanır; JPEG kalite denetimi WebP'de kayıplı kodlama sağlamaz.
- Çalışma alanları Rust panel sistemine uyarlanmıştır. Qt'nin bütün dock yerleşim ayrıntıları ve eski `.conf` içindeki oturum yerleşimi bire bir otomatik taşınmaz.
- Eski `.pdui` dosyasındaki bazı görünüm alanları (font ailesi, cetveller, menü/durum görünürlüğü, simge boyutu ve köşe yarıçapı) aktarımda saklanır; hepsi Rust görünümünü henüz yönetmez. Filmşeridi küçük resim satırı yoktur. Perspektif sayısal matris alanlarıyla ayarlanır; eski dört köşe nokta düzenleyicisi yoktur. Tuval boyutunda dokuz sabitleme noktası sunulur.

Bu farklar giderilmeden tablet/dock/yazdırma dahil yüzde yüz EA4 arayüz eşdeğerliği iddiası yapılmamalıdır. PSD/RAW/CMYK ve önceki sürümün desteklemediği profesyonel Photoshop iş akışları da bu çalışmanın kapsamına eklenmedi.
