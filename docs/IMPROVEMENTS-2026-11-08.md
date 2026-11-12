# PhotoDeck yenilikleri — 8 Kasım 2026

İlk denetimde ayrı öneri olarak hazırlanan liste, kullanıcının sonraki isteği ve **Klasik Pro + özgün PhotoDeck logosu** seçimi üzerine uygulamaya alındı. Aşağıdaki durumlar yerel geliştirme kaynaklarına aittir; yeni paket yayımlanmadı.

| Öncelik | Öneri | Gerçekleşen durum ve sınırı |
| --- | --- | --- |
| 1 | Uzun işlemlerde ilerleme, iptal ve arka plan işleme | Önizleme/filtre, raster/PDF/toplu çıktı ve kement işleri tek işçide çalışır. Gerçek aşama/sayaç gösterilir. İptal sonrası yeni çıktı yayımlanmaz; önceden biten çıktılar korunur. Codec/ICC/fsync tek çağrıları kontrol noktasına kadar bekleyebilir. |
| 2 | Kapatma/kayıt çakışmasında tam dosya yolu | Tam hedef ve kurtarma kaynağı gösterilir; sekmelerde yol ipucu ve pencere başlığında tam kaynak bulunur. |
| 3 | Başarısız diyalogda girişleri koruma | Metin, boyutlandırma, dönüşüm, efekt ve arka plan çıktı hata/iptal durumları girişleri korur. Başarısız commit sonrası yeni işlem açılarak dirty/history tutarlılığı korunur. |
| 4 | Araç adları, kısayollar ve ilk kullanım rehberi | İsim/kısayol ipuçları, F1 hızlı başlangıç ekranı ve kapatılabilir başlangıç ipucu eklendi. Klasik Pro iki sütun araçları kullanır; eski görünüm tercihleri korunur. |
| 5 | Büyük tuvalde kement performansı | Aktif kenarlı scanline hesabı ve tuval dışını atlama eklendi; önceki 4×4 even-odd kapsaması bağımsız referansla piksel eşleşir. Arka plan ilerleme/iptal yoluna bağlandı. |
| 6 | Son kullanılan dosyalar ve kurtarma kaynağı | En fazla 12 yerel son dosya; kurtarma sidecar'ında isteğe bağlı kaynak yolu. Eski String metadata ve Qt kayıtları okunur; kaynak dosya otomatik açılmaz. |
| 7 | Küçük ekran, ölçek, kontrast ve klavye | 800×560 / 1×, 1.5×, 2× native görüntüler ve panel sınır testleri yapıldı. Dar mantıksal genişlikte dock düğmeyle açılır. Gerçek OS klavye/fare otomasyonu hazır. İnsan katılımcılı kullanılabilirlik çalışması yapılmadı. |
| 8 | Tablet ve yerel yazıcı diyaloğu | Wayland tablet-v2 / Windows Ink basınç-eğim-silgi adaptörü, Linux Print portalı, Windows PrintDlgW/GDI ve macOS NSPrintOperation eklendi. **X11/macOS tablet adaptörü yok. Fiziksel tablet/yazıcı ve Windows/macOS çalışma zamanı doğrulanmadı.** |
| 9 | Parser fuzz ve çoklu OS GUI senaryoları | Ayrı cargo-fuzz workspace, Qt fixture seed'leri ve haftalık/kod değişimi CI eklendi. Yerel ASan koşusu 274 yürütme/21 saniye, bulgu yok. Linux/Windows/macOS için native klavye ve açılış/görüntü CI senaryoları tanımlandı; uzak CI bu görevde çalıştırılmadı. macOS Accessibility eksikliği hata kaydı olarak saklanır, geçer sayılmaz. |
| 10 | Bakımsız bağımlılıkların uyumlu yükseltilmesi | cosmic-text 0.19 ve rfd 0.17.2'ye geçildi; Rust 1.89 korundu. Audit 0 açık; uyarı 7→4. Kalan eframe/AccessKit/fontdb kaynaklı ttf-parser ve eski D-Bus zinciri, daha geniş framework/MSRV geçişi gerektirdiğinden korunuyor. |

Tasarımın uygulama durumu: **Klasik Pro yerel Rust arayüzüne uygulandı; taslaktaki Pd işareti yerine mevcut `packaging/photodeck.png` kullanıldı.** Menü, araç seçenekleri, belge sekmeleri, cetveller, renk/özellik/geçmiş dock'u, katman ve maske küçük resimleri gerçek uygulama kontrolleridir.

Kaynak ayrımı: `src/app/pro.rs` görünüm; `src/app/work.rs` güvenli UI iş entegrasyonu; `src/jobs.rs` tek işçi/iptal; `src/platform/` yerel tablet/yazdırma; `src/selection.rs` seçim hesaplaması. Son incelemede geçmiş gezinmesiyle thumbnail boyut değişimi, geç iptal sonucu, başarısız işlem tekrarında geçmiş kaybı ve kilitli katmanda sayısal dönüşüm girişleri için de regresyonlar eklendi.

Doğrulama ayrıntıları [Rust geçiş notlarında](RUST-MIGRATION.md), parser çalıştırma yöntemi [fuzz README'de](../fuzz/README.md), native OS senaryosu `scripts/native-ui-smoke.py` içindedir. Desteklenen özellik ile fiziksel cihaz/işletim sistemi üzerinde doğrulanmış davranış aynı şey değildir.

## Son yerel doğrulama

98 Rust testi, 9 paketleme testi, fmt, Clippy `-D warnings` ve release derlemesi geçti. Son release binary, izole X11/Xvfb ekranında gerçek OS klavye/fare olaylarıyla sınandı: katman ekle/kaydet, fareyle fırça çizimi/kaydet, çizimi geri alıp dosya byte eşitliği, katman geri al/yinele/kaydet ve yeniden aç/görüntü al adımları geçti. Bu bir insan veya fiziksel kalem testi değildir. Kanıt `artifacts/native-ui-linux-final/native-ui-proof.json`; son gerçek Wayland açılış görüntüsü `artifacts/classic-pro-final.png`. Artifacts git tarafından göz ardı edilir ve yerelde korunur.

Parser ASan kanıtı `artifacts/fuzz/project-decode-2026-11-08.log`. Native Windows tablet/yazdırma ve macOS yazdırma adaptörlerinin gerçek kaynakları ayrı harness ile çapraz tür kontrolünden geçti; tam uygulama çapraz derlemesi Little CMS C araç zinciri eksikliği nedeniyle tamamlanmadı. Uzak CI tetiklenmedi.
