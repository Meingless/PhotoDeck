# PhotoDeck 0.5.0 Early Access 1

AI içermeyen yerel fotoğraf ve katman düzenleyici. Erken erişim sürümüdür; önemli orijinalleri koruyun ve ayrı proje kopyalarıyla çalışın.

Varsayılan görünüm **3 · Odak**. Çalışma alanı menüsünden 10 preset arasında geçilebilir. Görünümü özelleştir veya Ctrl+Alt+P; yazı tipi/boyutu, simge boyutu, renkler, köşeler, panel tarafı/genişliği, araç adları, cetveller, menü/durum çubukları ve panel taşıma ayarlarını değiştirir. Dock panellerini başlıklarından taşıyıp ayırabilirsiniz. Odak'taki yüzen paneller sürüklenebilir; taşıma tutamacı odaktayken ok tuşları da çalışır. Kendi presetlerinizi kaydedebilir veya .pdui olarak taşıyabilirsiniz.

Görünüm ayarları bilgisayarın standart uygulama yapılandırma klasöründeki PhotoDeck/ui.ini dosyasında saklanır. .pdui yalnızca doğrulanan görünüm değerleri içerir; betik veya CSS çalıştırmaz. Belge pikselleri görünüm presetinden bağımsızdır.

## Paketler

- Linux x86_64: arşivi açın ve launch.sh çalıştırın. Qt kütüphaneleri pakette bulunur; sistem glibc, grafik sürücüsü ve fontlar sistemden kullanılır. CI derlemesi Ubuntu 22.04/glibc 2.35 tabanını hedefler.
- Windows x64: ZIP'i tamamen açın; PhotoDeck.exe çalıştırın. Qt DLL'leri ve görüntü eklentileri uygulamayla birlikte gelir. VC++ çalışma zamanı DLL'leri uygulamanın yanına eklenir. Windows 10/11 x64 hedeflenir.
- macOS Intel / Apple Silicon: ZIP'i açıp PhotoDeck.app uygulamasını Applications klasörüne taşıyın. Erken erişim paketleri Apple Developer kimliğiyle imzalanmış veya noter onaylı değildir; macOS ilk açılışta güvenlik onayı isteyebilir. Ad hoc imza yayıncı kimliğini doğrulamaz.

PNG/JPEG/WebP/BMP, TIFF ve .pdeck desteklenir; GIF ilk kare olarak açılır. Belge RGB 8 bit olarak çalışır; yüksek bit derinlikli görüntüler 8 bite dönüşür. Çok sayfalı TIFF'in ilk görüntüsü açılır. PSD/PSB, RAW, CMYK/ICC, ileri vektör yolları ve tablet basıncı hâlâ açık özellik hedefleridir. 16 MP, 64 katman, 256 MiB piksel/maske ve 30 durum/128 MiB ek geçmiş belleği sınırları vardır. Kurtarma kopyası 45 saniyede bir alınır; son değişiklikleri her zaman kapsamayabilir.

Derleme sonucu ile hedef donanım testi farklıdır: CI testleri ve paket açılış görüntüleri sağlanır; minimum donanım gereksinimi, bütün Linux dağıtımları, Windows sürümleri ve macOS cihazları test edilmiş değildir. SHA256SUMS.txt indirdiğiniz paketin bütünlüğünü kontrol eder.

Kaynak ve lisanslar paketle birlikte hazırlanır. Üçüncü taraf yazılım bildirimleri THIRD-PARTY.md ve licenses/ içindedir.

0.3 ilk Photoshop karşılaştırma aşamasıdır: gerçek alfa seçimleri, eliptik seçim, kementler, toleranslı sihirli değnek, ekle/çıkar/kesiştir, ters çevirme, büyütme/küçültme ve feather; 27 harmanlama modu; seviyeler, RGB/kanal eğrileri, pozlama, hue/vibrance, temel renk dengesi ve invert/threshold/posterize ekler. Ton ayarları etkin katmanın kaynak piksellerini korur; ayrı ayar katmanları henüz yoktur. Eğriler parça parça doğrusal, feather kutu yumuşatmasıdır.

Ctrl+L ton penceresi; Shift+M eliptik seçim, L kement, Shift+L çokgen kement, W değnek. Çokgen Enter/çift tıklamayla tamamlanır, Esc iptal eder. Seçimde Shift ekle, Alt çıkar, Shift+Alt kesiştir. Ctrl+Shift+I seçimi ters çevirir. Seçimler henüz geçicidir; proje/geçmişe kaydedilmez.

Yeni projeler .pdeck sürüm 4'tür. Sürüm 1/2/3 projeleri açılır; eski uygulamalar sürüm 4 projelerini açamaz. Orijinalleri ayrı koruyun.

Linux AppImage tek dosyalık alternatifidir: çalıştırma izni verin; FUSE yoksa `APPIMAGE_EXTRACT_AND_RUN=1 ./PhotoDeck-0.5.0-ea.1-x86_64.AppImage`. Kütüphaneler AppImage içindedir.

Photoshop karşılaştırması ve kapanmamış farklar docs/PHOTOSHOP-COMPARISON.md içindedir. Bu sürüm tam Photoshop eşdeğerliği değildir.

0.4 ekleri: Ctrl+T ile kaynak koruyan sayısal ölçek/dönüş/eğme ve dört köşeli perspektif; Ctrl+Alt+C ile dokuz sabitleme noktalı tuval boyutu; Ctrl+Shift+E ile görünür katmanları birleştirme. Birleştirme tuval dışı pikselleri ve gizli katmanları korur. Tuvale düzleştirme gizli katmanları ve tuval dışını kaldırır, saydamlığı korur. Bütün işlemler geri alınabilir.

Dönüşüm sırasında metin ve maskeler kaynakta kalır. Boyama/silme için Katman → Görünümü rasterleştir gerekir; bu adım maske/renk ayarlarını uygular ve metni piksele dönüştürür. Tüm görüntü boyutlandırma/90° dönüş/aynalaması da dönüştürülmüş katmanları rasterleştirir. Tuval üstü dönüşüm tutamaçları ve warp henüz yoktur. Kaynak verilerinden ayrı, toplam dönüşmüş görünüm alanı 256 MiB ile sınırlanır.

0.5: Ctrl+Alt+G kırpma maskesi; Ctrl+J seçim yoksa çoğaltma, varsa seçimi yeni katmana kopyalama; Ctrl+Shift+J seçimi kesme; Ctrl+E alta birleştirme. Temel katmanın alfa/maskesi/görünürlüğü ve opaklığı kırpma zincirine uygulanır. Kopya/cut maske, renk ayarları ve dönüşümü raster piksellere uygular; cut kaynak metni de rasterleştirir. Feather cut kenarı, sert seçim gibi aynı alfa sonucunu garanti etmez.

Kilitli veya kırpma zincirindeki katmandan kesme; gizli kırpılan çocuğu olan görünür grupta merge-visible; bağımlı blend/clip görünümünü değiştiren merge-down reddedilir. Kırpma ilişkisini kaldırma/görünürlüğü değiştirme veya görünür birleştirme kullanılabilir. Temel silinirse çocukların kırpması kaldırılır; katman sırası kırpma temelini belirler. Bu sınırlar tam Photoshop parity olarak işaretlenmez.
