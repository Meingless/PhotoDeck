# PhotoDeck erken erişim ve geliştirme notları

Bu kaynak dalı **0.10.0-ea.2** erken erişim adayını hazırlar. Aşağıdaki 0.9.1 ve önceki sürüm kayıtları tarihsel kapsamı anlatır; 0.10 için platform/paket doğrulaması sayılmaz. Yeni davranışlar ve doğrulama sınırları [geliştirme notlarında](docs/DEVELOPMENT-0.10.md) izlenir.

AI içermeyen yerel fotoğraf ve katman düzenleyici. Erken erişim sürümüdür; önemli orijinalleri koruyun ve ayrı proje kopyalarıyla çalışın.

## 0.10.0-ea.2 — güncel kapsam

- Tuvalden görünür piksele göre hedef katman seçimi ve görünüm sınırı; canlı kaynak/görünüm ölçüleri, açık sayısal oran kilidi ve dar pencerede okunur dönüşüm eylemleri eklendi.
- Seçimi kopyala ve dönüştür, görünen seçili pikselleri bağımsız kopyada önizler; özgün yerinde kalır. Esc kopyayı iptal eder, Uygula kopya ile dönüşümü tek geri alma adımı yapar.
- Dönüşümden boyamaya geçişte kalıcı açıklama, onaylı rasterleştirme veya özgünü koruyan yeni boş boya katmanı sunulur. Görüntü içe aktarma ve dar Dokunmatik düzeni iyileştirildi.

- Seçim işlemleri geri alınır; Ctrl+D seçimi kaldırır, Ctrl+Shift+D yeniden seçer. Seçim menüsünde açıkça adlandırılmış seçimler kaydedilir/yüklenir/silinir. [Seçim kılavuzu](docs/SELECTION-WORKFLOW.md).
- Ctrl+T bütün etkin katmanda tuval üstü taşıma/ölçek/dönüş tutamaçlarını; Ctrl+Alt+T sayısal/perspektif ayarlarını açar. Enter uygular, Esc iptal eder. [Dönüşüm kılavuzu](docs/ON-CANVAS-TRANSFORM.md).
- Liste içinde sürükleyerek tek katman sıralanır; bırakma çizgisi, dar listede kenar kaydırması ve Esc ile iptal vardır. [Katman kılavuzu](docs/LAYER-WORKFLOW.md).
- F2 katman adını satır içinde düzenler; bağlam menüsü açılan satırı hedefler. Sayısal X/Y, tuval/seçim sınırına yatay-dikey hizalama ve odaklı tuvalde Taşı aracıyla ok/Shift+ok = 1/10 belge pikseli vardır. [Konumlandırma kılavuzu](docs/LAYER-POSITIONING.md).
- [Geçmiş paneli](docs/HISTORY-NAVIGATION.md) geri/ileri adımlara fare ve klavyeyle gider. [Kurtarma](docs/RECOVERY.md) görüntü kodlama ve büyük yazımı arka planda, süreç genelinde tek değişmez anlık görüntü/kodlayıcıyla yapar. Normal Kaydet eşzamanlı kalır; büyük belgelerde bekleyebilir.

Normal projeler adlandırılmış seçim yoksa v5 olarak kalır; yalnız geçici seçim dosyayı yükseltmez. Adlandırılmış seçimler v6 gerektirir ve yayımlanan 0.9.1 bunları açamaz. İlk v6 kaydında uyarı verilir; eski orijinal ayrı kopyayla korunur. Son adlandırılmış seçim silinince normal kayıt yeniden v5 olur. Kurtarma seçim bağlamı için ayrıca v6 kullanabilir; normal projenin kayıt kimliğini değiştirmez.

Bu kapsam PSD/RAW, CMYK/yüksek bit, grup veya ayar katmanları eklemez. Kayıtlı seçimler genel amaçlı alfa Channels paneli değildir; dönüşüm varsayılan olarak bütün etkin katmanı kapsar; seçimi kopyala ve dönüştür özgünü koruyan ayrı raster kopya oluşturur. Yerinde seçili piksel dönüşümü ve warp sağlamaz. Bu sürüm tam Photoshop eşdeğeri değildir. Kaynak testleri [özellik denetiminden](docs/FEATURE-AUDIT.md), gerçekten çalıştırılan commit/ortam ve sınırlar [0.10 geliştirme notlarından](docs/DEVELOPMENT-0.10.md) izlenir. Aşağıdaki tarihsel paket sonuçları 0.10'u doğrulamaz.

## 0.10.0-ea.2 paketleri ve doğrulama

[Sürüm kaydı](https://github.com/Meingless/PhotoDeck/releases/tag/v0.10.0-ea.2), test edilen tam kaynak commit'ini, başarılı CI/paket akışlarını, SHA256SUMS.txt ve RELEASE-PROOF.json dosyalarını içerir. Yayımlayıcı dört platformda 22/22 native CTest grubu ve ayrı Linux ASan/UBSan işi başarılı olmadan yayını açmaz. Linux/Windows'ta 22 grubun tamamı paketlenmiş Qt ile ayrıca çalışır; macOS'ta native CTest, dağıtılmış uygulama açılışı ve ad hoc imza kontrolü yapılır. Atlanan platforma özgü vakalar kanıt dosyasında ad ve gerekçeyle listelenir; bunlar başarılı vaka sayılmaz.

- Linux x86_64 portable arşivi: tamamen açıp launch.sh çalıştırın. AppImage için çalıştırma izni gerekir; FUSE yoksa APPIMAGE_EXTRACT_AND_RUN=1 kullanılabilir. CI paketi Ubuntu 22.04 tabanında yeniden derlenir; önceki glibc 2.39 isteyen bulut geliştirme paketi yayımlanmaz.
- Windows x64: ZIP'i tamamen açıp PhotoDeck.exe çalıştırın. Qt ve VC++ çalışma zamanı DLL'leri yanındadır. Yayıncı sertifikasıyla imzalı değildir.
- macOS Intel / Apple Silicon: mimarinize uygun ZIP'i açın, PhotoDeck.app klasörünün tamamını koruyun. Yalnız ad hoc imzalıdır; Apple Developer ID veya noter onayı yoktur. Sistem güvenlik uyarısı verebilir.

Native CI ve açılış görüntüleri fiziksel tablet/yazıcı, tüm işletim sistemi sürümleri veya gerçek cihaz matrisi doğrulaması değildir. Qt SDK sanitizer-instrument edilmedi ve ASan leak detection kapalıdır. Tam GUI ThreadSanitizer koşusunda önceden görülen dosya-penceresi uyarısı çözülmüş veya temiz ilan edilmiş değildir. Normal Kaydet eşzamanlıdır; kurtarma sıfır veri kaybı garantisi vermez.

## Ortak çalışma alanı kullanımı

Varsayılan görünüm **3 · Odak**. Çalışma alanı menüsünden 10 preset arasında geçilebilir. Görünümü özelleştir veya Ctrl+Alt+P; yazı tipi/boyutu, simge boyutu, renkler, köşeler, panel tarafı/genişliği, araç adları, cetveller, menü/durum çubukları ve panel taşıma ayarlarını değiştirir. Dock panellerini başlıklarından taşıyıp ayırabilirsiniz. Odak'taki yüzen paneller sürüklenebilir; taşıma tutamacı odaktayken ok tuşları da çalışır. Kendi presetlerinizi kaydedebilir veya .pdui olarak taşıyabilirsiniz.

Görünüm ayarları bilgisayarın standart uygulama yapılandırma klasöründeki PhotoDeck/ui.ini dosyasında saklanır. .pdui yalnızca doğrulanan görünüm değerleri içerir; betik veya CSS çalıştırmaz. Belge pikselleri görünüm presetinden bağımsızdır.

## 0.9.1-ea.1 — tarihsel güncelleme

Bu bölüm ve devamındaki paket/0.3–0.9 notları yayımlanan 0.9.1 kapsamını kaydeder. Güncel seçim, dönüşüm, kısayol ve kurtarma davranışı yukarıdaki 0.10 bölümünde ayrıca açıklanır.

0.9.1; tuvali panellerden ayıran Odak düzeni, araca bağlı seçenek çubuğu, Ctrl+Alt+1…0 çalışma alanı geçişi ve küçük resimlerin yeniden kullanımını getirir. Dışarıdan değiştirilen/silinen projeye kayıtta iptal, ayrı kopya veya açıkça üzerine yazma seçilebilir. v1–v4 orijinaline kaydetme girişimi uyarıdan sonra farklı adla v5 kopyaya yönlendirilir; .pdeck v5 değişmedi. Hakkında sürümü macOS'ta görünür içerikte de gösterilir. [Sürüm notları](docs/releases/0.9.1-ea.1.md) kayıt/kurtarma ve doğrulama sınırlarını açıklar.

0.9.1'de dosya çakışması kontrolü iyimseldir; denetim ile atomik kayıt arasındaki dar yarış aralığı tamamen kapanmaz. O sürümde normal kayıt ve kurtarmanın PNG kodlama/dosya yazımı eşzamanlıdır; büyük belgelerde arayüz bekleyebilir. Kurtarma sıfır veri kaybı garantisi değildir.

### 0.9.1 erken erişim paketleri — tarihsel

- Linux x86_64: arşivi açın ve launch.sh çalıştırın. Qt kütüphaneleri pakette bulunur; sistem glibc, grafik sürücüsü ve fontlar sistemden kullanılır. CI derlemesi Ubuntu 22.04/glibc 2.35 tabanını hedefler.
- Windows x64: ZIP'i tamamen açın; PhotoDeck.exe çalıştırın. Yayıncı sertifikasıyla imzalanmış değildir. Qt DLL'leri ve görüntü eklentileri uygulamayla birlikte gelir. VC++ çalışma zamanı DLL'leri uygulamanın yanına eklenir. Windows 10/11 x64 hedeflenir.
- macOS Intel / Apple Silicon: ZIP'i açıp PhotoDeck.app uygulamasını Applications klasörüne taşıyın. Erken erişim paketleri Apple Developer kimliğiyle imzalanmış veya noter onaylı değildir; macOS ilk açılışta güvenlik onayı isteyebilir. Ad hoc imza yayıncı kimliğini doğrulamaz.

PNG/JPEG/WebP/BMP, TIFF ve .pdeck desteklenir; GIF ilk kare olarak açılır. Belge RGB 8 bit olarak çalışır; yüksek bit derinlikli görüntüler 8 bite dönüşür. Çok sayfalı TIFF'in ilk görüntüsü açılır. PSD/PSB, RAW, CMYK/proofing, ileri vektör yolları ve fiziksel tablet cihaz matrisi hâlâ açık özellik hedefleridir. 16 MP, 64 katman, 256 MiB piksel/maske ve 30 durum/128 MiB ek geçmiş belleği sınırları vardır. Kurtarma tamamlanan düzenlemelerden yaklaşık 5 saniye sonra ve 45 saniyelik periyodik kontrolde denenir. Süren hareket bitene kadar ertelenir; son değişiklikleri her zaman kapsamayabilir.

Derleme sonucu ile hedef donanım testi farklıdır: CI testleri ve paket açılış görüntüleri sağlanır; minimum donanım gereksinimi, bütün Linux dağıtımları, Windows sürümleri ve macOS cihazları test edilmiş değildir. SHA256SUMS.txt indirdiğiniz paketin bütünlüğünü kontrol eder.

Kaynak ve lisanslar paketle birlikte hazırlanır. Üçüncü taraf yazılım bildirimleri THIRD-PARTY.md ve licenses/ içindedir.

### 0.3–0.9 uygulama geçmişi — 0.9.1 kapsamı

Aşağıdaki eski kısayollar, biçim ve açık özellik sınırları ilgili sürümlerin tarihsel durumudur; 0.10'da değişen seçim geçmişi, kayıtlı seçimler ve tuval üstü dönüşüm kapsamı yukarıdadır.

0.3 ilk Photoshop karşılaştırma aşamasıdır: gerçek alfa seçimleri, eliptik seçim, kementler, toleranslı sihirli değnek, ekle/çıkar/kesiştir, ters çevirme, büyütme/küçültme ve feather; 27 harmanlama modu; seviyeler, RGB/kanal eğrileri, pozlama, hue/vibrance, temel renk dengesi ve invert/threshold/posterize ekler. Ton ayarları etkin katmanın kaynak piksellerini korur; ayrı ayar katmanları henüz yoktur. Eğriler parça parça doğrusal, feather kutu yumuşatmasıdır.

Ctrl+L ton penceresi; Shift+M eliptik seçim, L kement, Shift+L çokgen kement, W değnek. Çokgen Enter/çift tıklamayla tamamlanır, Esc iptal eder. Seçimde Shift ekle, Alt çıkar, Shift+Alt kesiştir. Ctrl+Shift+I seçimi ters çevirir. Seçimler henüz geçicidir; proje/geçmişe kaydedilmez.

Yeni projeler .pdeck sürüm 5'tir. Sürüm 1/2/3/4 projeleri açılır; eski uygulamalar sürüm 5 projelerini açamaz. Orijinalleri ayrı koruyun.

Linux AppImage tek dosyalık alternatifidir: çalıştırma izni verin; FUSE yoksa `APPIMAGE_EXTRACT_AND_RUN=1 ./PhotoDeck-0.9.1-ea.1-x86_64.AppImage`. Kütüphaneler AppImage içindedir.

Photoshop karşılaştırması ve kapanmamış farklar docs/PHOTOSHOP-COMPARISON.md içindedir. Bu sürüm tam Photoshop eşdeğerliği değildir.

0.4 ekleri: Ctrl+T ile kaynak koruyan sayısal ölçek/dönüş/eğme ve dört köşeli perspektif; Ctrl+Alt+C ile dokuz sabitleme noktalı tuval boyutu; Ctrl+Shift+E ile görünür katmanları birleştirme. Birleştirme tuval dışı pikselleri ve gizli katmanları korur. Tuvale düzleştirme gizli katmanları ve tuval dışını kaldırır, saydamlığı korur. Bütün işlemler geri alınabilir.

Dönüşüm sırasında metin ve maskeler kaynakta kalır. Boyama/silme için Katman → Görünümü rasterleştir gerekir; bu adım maske/renk ayarlarını uygular ve metni piksele dönüştürür. Tüm görüntü boyutlandırma/90° dönüş/aynalaması da dönüştürülmüş katmanları rasterleştirir. Tuval üstü dönüşüm tutamaçları ve warp henüz yoktur. Kaynak verilerinden ayrı, toplam dönüşmüş görünüm alanı 256 MiB ile sınırlanır.

0.5: Ctrl+Alt+G kırpma maskesi; Ctrl+J seçim yoksa çoğaltma, varsa seçimi yeni katmana kopyalama; Ctrl+Shift+J seçimi kesme; Ctrl+E alta birleştirme. Temel katmanın alfa/maskesi/görünürlüğü ve opaklığı kırpma zincirine uygulanır. Kopya/cut maske, renk ayarları ve dönüşümü raster piksellere uygular; cut kaynak metni de rasterleştirir. Feather cut kenarı, sert seçim gibi aynı alfa sonucunu garanti etmez.

Kilitli veya kırpma zincirindeki katmandan kesme; gizli kırpılan çocuğu olan görünür grupta merge-visible; bağımlı blend/clip görünümünü değiştiren merge-down reddedilir. Kırpma ilişkisini kaldırma/görünürlüğü değiştirme veya görünür birleştirme kullanılabilir. Temel silinirse çocukların kırpması kaldırılır; katman sırası kırpma temelini belirler. Bu sınırlar tam Photoshop parity olarak işaretlenmez.

0.6: Filtre → Gaussian, keskinleştirme ve klasik filtreler; katman ölçeğinde Gaussian, unsharp miktar/yarıçap/eşik, high-pass, seeded uniform mono/color gren ve mosaic. Kaynak pikseller/metin korunur, canlı önizleme kapatılır, iptal ve tek undo vardır. Maske yoğunluğu/feather/enable kaynak maskeyi değiştirmez. Radius sınırı 16 px; Gaussian sigma max(0.5,radius/2), edge-clamp kullanır. Sıra mosaic→noise→Gaussian→high-pass→unsharp; ton ayarı önce, maske/dönüşüm sonra. Büyük radius/görüntü CPU ve bellek kullanımı artırır. Reorderable smart-filter stack, ayrı filter mask, lens/motion blur, smart sharpen ve native Photoshop presets henüz yoktur.

0.7: Dosya menüsünden boyut/DPI/kalite ve RGB ICC profiliyle raster çıktı, tek sayfalı PDF veya native yazdırma seçilebilir. RGB profilli görüntü girdileri sRGB'ye dönüştürülür; etiketsiz girdiler sRGB kabul edilir. PNG/JPEG/TIFF/WebP için sRGB/Display P3/Adobe RGB profili eklenebilir. Çıktı boyutu belgeyi değiştirmez. DPI dosya metadata desteğine bağlıdır; piksel ayrıntısını artırmaz. PDF A4/Letter/fiziksel görüntü boyutu, yön ve kenar boşluklarını destekler; görüntü oranını korur, şeffaflığı beyaz kağıda uygular. PDF katmanlı/vektör/PDF-X değildir. Fiziksel yazıcı, soft proof, monitör profili, CMYK/Lab ve yüksek bit derinliği henüz doğrulanmış/desteklenmiş değildir.

0.8: Ctrl+Alt+B fırça/tablet ayarlarını açar. Uç açısı, yuvarlaklık ve darbe aralığı; basınç boyutu/akışı, minimum boyut ve tepki eğrisi; eğime bağlı açı/yuvarlaklık vardır. Kalemin silgi ucu piksel ve maske araçlarına uygulanır. Tek hareket tek undo; iptal kaynak pikselleri geri getirir. Alfa seçim her darbeye uygulanır. Fare tam basınçtır. Ayarlar sekme oturumuna aittir, .pdeck biçimi değişmedi. Fiziksel tablet testi yapılmadı; Qt olaylarıyla davranış doğrulandı. Özel görüntü uçları/ABR, fırça kütüphanesi, scatter/jitter/texture ve simetri açık kalır.

0.9: Arayüz ortak modern yüzeyler, tipografi, menüler, sekmeler, panel/listeler, girişler ve görünür odak/seçim durumlarıyla yenilendi; 3 · Odak varsayılanı, on preset ve kişisel .pdui ayarları korunur. Düzenle menüsündeki Fırça kütüphanesi veya Ctrl+Alt+Shift+B isimli fırçaları kalıcı kaydeder, yeniden seçer, siler ve .pdbrush ile taşır. Görselden/seçimden uç oluşturulur; siyah/koyu boyar, beyaz/saydam gizler. Uzun kenar 512 px, koleksiyon 128 preset/8 MiB maskeyle sınırlıdır. Özel uçta sertlik maskeden gelir. Klon kaynağı çevrilmez; sadece uç kapsamı çevrilir. Değerler ve araç presetle saklanır; ön plan rengi ve klon kaynak konumu saklanmaz. .pdeck v5 değişmedi. ABR ve gelişmiş fırça dinamikleri açık hedeflerdir.
