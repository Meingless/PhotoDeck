# PhotoDeck 0.5.0 Early Access

AI içermeyen, tamamen yerel, C++/Qt 6 ile geliştirilen fotoğraf ve katman düzenleyici. Linux, Windows ve macOS için ortak kaynak kullanır. Tarayıcı motoru, Python çalışma zamanı, bulut hesabı, abonelik veya ağ servisi gerekmez.

## Linux'ta kullan

Hazır uygulama için `./launch.sh` çalıştırın. `./scripts/install-user.sh` uygulama menüsüne kısayol ekler. Yerel geliştirici derlemesi `bin/photodeck`, x86_64 Linux ve sistem Qt kütüphanelerini kullanır. Qt uygulamayla birlikte taşınmadığından küçük paketin boyutu sistem Qt bağımlılıklarını içermez. Dağıtım paketleri ve AppImage ise Qt'yi içerir.

Sistem paketleri: CachyOS/Arch için `qt6-base qt6-imageformats`; Ubuntu/Debian'da kaynak derlemek için `build-essential cmake qt6-base-dev qt6-base-dev-tools qt6-image-formats-plugins`.

## Görünüm presetleri ve kişiselleştirme

Varsayılan görünüm **Odak**. Çalışma alanı menüsünde Grafit, Kağıt, Odak, Fotoğraf Masası, Çalışma Tezgahı, Şerit, Çizim Atölyesi, Kompakt, Dokunmatik ve Modüler presetleri bulunur. **Ctrl+Alt+P** görünüm düzenleyicisini açar; renkler, yazı ve simge boyutu, köşeler, panel yerleşimi ve görünürlük düzenlenebilir. Paneller taşınabilir/ayrılabilir, ayarlar oturumlar arasında korunur; kişisel presetler kaydedilebilir ve .pdui ile içe/dışa aktarılabilir. Erken erişim kurulum notları [EARLY-ACCESS.md](EARLY-ACCESS.md) içinde.

## Araçlar

- Aynı pencerede birden fazla belge, sürükleyerek dosya açma ve panodan görüntü katmanı ekleme.
- Piksel ve düzenlenebilir metin katmanları; taşıma, adlandırma, çoğaltma, sıralama, görünürlük ve kilit.
- Katman opaklığı ve 27 harmanlama modu: normal, çarp, ekran, kaplama, yakma/soldurma, ışık, fark, hue/saturation/color/luminosity dahil.
- Alt katmana kırpma maskesi (Ctrl+Alt+G): ardışık kırpılan katmanlar temel katmanın alfa/maskesini kullanır; temel görünürlük ve opaklık tüm zincire uygulanır. Katman listesinde ↳ işareti gösterilir.
- Ctrl+J seçim yoksa tüm katmanı, seçim varsa etkin katmanın seçimini yeni katmana kopyalar. Ctrl+Alt+J aynı seçim komutunu açar; Ctrl+Shift+J seçimi keser. Maskeler/renk ayarları/dönüşüm kopya piksellere uygulanır; kesmede kaynak katman da rasterleştirilir. Kesme kilitli veya kırpma zinciri içindeki katmanlarda reddedilir; ilişkiyi kaldırıp işlemi tekrar yapabilirsiniz.
- Ctrl+E alta birleştirir; işlem önce/sonra görünümü karşılaştırır. Görünümü değiştirecek bağımlı harmanlama/kırpma zincirleri reddedilir; görünür katmanları birleştirme alternatifi vardır.
- Beyazın gösterip siyahın gizlediği katman maskeleri. Seçimden maske oluşturma, maskeyi ters çevirme veya kaldırma.
- Fırça, kalem, silgi, aynı katmandan klon damgası, bağlı alana boya kovası, iki renkli degrade, dikdörtgen, elips ve damlalık.
- Dikdörtgen/eliptik seçim, serbest/çokgen kement ve toleranslı sihirli değnek. Yeni/ekle/çıkar/kesiştir; ters çevirme, kenar yumuşatma ve büyütme/küçültme. Seçim alfası çizim, silme, kopyalama ve maske oluşturma işlemlerinde korunur.
- Seçili piksel silme, kırpma, oranı koruyan yeniden boyutlandırma, 90° dönüş ve aynalama.
- Ctrl+T ile kaynak koruyan katman dönüşümü: ölçek, keyfi açı, eğme, aynalama ve dört köşeli perspektif. Kaynak piksel, metin ve maske korunur; önizleme iptal edilebilir ve tek adımda geri alınır.
- Dokuz sabitleme noktasıyla tuval boyutu; tuval dışındaki kaynak pikselleri silmez. Görünür katmanları birleştirme tuval dışındaki alanları ve gizli katmanları korur. Tuvale düzleştirme saydamlığı koruyup gizli katmanları ve tuval dışını kaldırır.
- Kaynak pikselleri koruyan parlaklık, kontrast, doygunluk, siyah beyaz ve kutu bulanıklığı.
- Seviyeler (giriş/çıkış aralığı, gamma), pozlama, RGB ve ayrı kanal eğrileri; hue, vibrance, temel kanal renk dengesi, invert, threshold ve posterize. Ctrl+L ile canlı önizleme; iptal geri döner, kabul tek geri alma adımıdır.
- Yerel fontlarla çok satırlı metin, boyut, kalınlık ve renk. Metin katmanına çift tıklayarak düzenleme.
- PNG/JPEG/WebP/BMP ve TIFF içe/dışa aktarma; GIF ilk kare içe aktarımı. WebP/TIFF sistemin Qt görüntü eklentisine bağlıdır. Çalışma belgesi RGB 8 bittir; yüksek bit derinlikli girdiler 8 bite dönüşür. Çok sayfalı TIFF/animasyonlu GIF yalnızca ilk görüntüyü açar.
- Saydam PNG/WebP; JPEG saydam alanları beyaz arka plan üzerine birleştirir.
- Orijinalleri değiştirmeyen toplu boyutlandırma, renk ayarı ve biçim dönüşümü. Çıktı adlarında `_photodeck` eki kullanılır; mevcut çıktılar için sayı eklenir.
- Atomik `.pdeck` proje kaydı; katman, maske, metin, konum, harmanlama ve renk/filtre parametreleri korunur.
- 45 saniyelik kurtarma kopyaları; sonraki açılışta kurtarma önerisi. Normal kapatmada kaydet/at/iptal seçimi.

Kısayollar: V taşı, M seç, C kırp, B fırça, P kalem, E silgi, S klon, F kova, G degrade, T metin, U dikdörtgen, O elips, I damlalık, H el, X renk değiştir. Klon kaynağı Alt+tıklama. Kırpmayı Ctrl+Shift+X ile uygula. Ctrl/Cmd+Z geri alır; Ctrl/Cmd+0 sığdırır; Ctrl/Cmd+1 gerçek boyutu gösterir. Tekerlek yakınlaştırır, orta tuş tuvali kaydırır. Bazı özel kısayollar platformdan bağımsız Ctrl olarak tanımlanmıştır; macOS'ta standart Dosya/Düzenle kısayolları Cmd kullanır.

Shift+M eliptik seçim, L kement, Shift+L çokgen kement, W sihirli değnek. Çokgen kement Enter/çift tıklamayla tamamlanır, Esc iptal eder. Seçim sırasında Shift ekler, Alt çıkarır, Shift+Alt kesiştirir; araç seçeneklerinden aynı modlar seçilebilir. Ctrl+Shift+I seçimi ters çevirir. Seçimler bu sürümde geçicidir; proje veya belge geçmişine kaydedilmez.

0.5 projeleri `.pdeck` sürüm 4 olarak kaydedilir; eski sürüm 1/2/3 projeleri açılabilir. Eski uygulamalar sürüm 4 dosyalarını açamaz; orijinalleri ayrı saklayın. v2/v3 uyumluluk örnekleri yayımlanmış 0.3/0.4 AppImage'larıyla oluşturuldu.

Kırpma zinciri katman sırasına bağlıdır. Temel katmanı silmek çocukların kırpmasını kaldırır; sıralama/yeni katman yerleştirme hangi temel katmana kırpıldığını değiştirebilir. Gizli temelli gruplar görünür birleştirmede korunur. Görünür temeli ve gizli kırpılan çocuğu olan gruplar birleştirilmeden önce ayrılmalı veya görünür yapılmalıdır; ilişkiler sessizce kaybedilmez. Feather ile kesme, kısmi alfa sınırlarında iki ayrı katman oluşturduğu için sert seçim gibi piksel piksel aynı görünümü garanti etmez.

Dönüşmüş katmanda piksel boyamak/silmek için Katman → Görünümü rasterleştir kullanın. Bu açık adım dönüşümü, maskeyi ve renk ayarlarını kaynak piksellere uygular; metin düzenlenebilirliği kaldırılır, görünür sonuç ve geri alma korunur. Tüm görüntüye boyutlandırma/90° dönüş/aynalama uygulanması da dönüştürülmüş katmanları önce rasterleştirir. Serbest dönüşüm şu an sayısal bir pencereyle yapılır; tuval üstü tutamaçlar ve warp henüz yoktur.

Açılıştaki Horizon kompozisyonu PhotoDeck için kodla çizilmiş özgün bir örnektir. `examples/Horizon.pdeck` dosyası dört ayrı düzenlenebilir katman içerir.

## Hafiflik ve güvenlik sınırları

Tuval en fazla 16 megapiksel, her kenar en fazla 16384 piksel. En fazla 64 katman ve 256 MiB kaynak piksel/maske verisi; katmanların toplam dönüşmüş görünüm alanı da 256 MiB ile sınırlıdır. Geçmiş en fazla 30 durum ve canlı belgeden ayrı toplam 128 MiB benzersiz piksel verisi saklar; büyük belgelerde eski adımlar daha erken atılır. Paylaşılan görüntü tamponları gereksiz kopyaları azaltır; filtre sonuçları önbelleğe alınır. Bu sınırlar toplam süreç RAM'inin üst sınırı değildir: tuval önizlemesi, filtre/dönüşüm tamponları, Qt ve dosya kodlama ek bellek kullanır.

Bozuk proje okumaları geçici belge üzerinde doğrulanır ve açık belgeyi değiştirmez. Uzunluk, boyut, katman sayısı, maske boyutu ve sayısal parametreler sınırlanır. Proje dosyalarında komut, betik veya harici kaynak çalıştırılmaz. Başarısız kayıt açık belgenin kaydedilmiş durumunu değiştirmez. Proje/geçmiş ayrıdır: undo geçmişi dosyaya yazılmaz. Kurtarma kopyaları son 45 saniyedeki değişiklikleri her zaman kapsamayabilir; büyük belgelerde PNG kodlama arayüzde kısa beklemeye neden olabilir.

## Linux / Windows / macOS derleme

Qt **6.5 veya sonrası**, C++17 derleyicisi ve CMake 3.21+ gerekir. Testler için Qt Test modülü gerekir. CMake'ye Qt yolunu gerekirse `-DCMAKE_PREFIX_PATH=/Qt/6.8.3/...` ile verin.

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --config Release --parallel 4
ctest --test-dir build-cmake -C Release --output-on-failure
```

Linux için `./scripts/build.sh` CMake varsa onu, yoksa qmake6 + make kullanır ve testleri çalıştırır. Alternatif: ayrı bir build klasöründe `qmake6 ../PhotoDeck.pro && make -j4`.

Windows'ta Qt MSVC veya MinGW kitiyle derleyin; `windeployqt` Qt DLL'lerini ve eklentilerini uygulamanın yanına koyar. MSVC/CMake çıktısı `build-cmake/Release/photodeck.exe` olur. macOS'ta çıktı `build-cmake/photodeck.app`; `macdeployqt` bağımlılıkları `.app` içine yerleştirir. Dağıtım için Windows imzalama ve macOS imzalama/notarization ayrıca gerekir.

`.github/workflows/build.yml` üç işletim sistemi için derleme, işlev testleri ve paket hazırlama akışını içerir. Erken erişim akışı Linux x86_64, Windows x64 ve macOS Intel/Apple Silicon için ayrı paketler, test raporları ve açılış görüntüleri üretir. Gerçek run sonuçları artifacts/early-access içinde kaydedilir.

Linux paketleme: `./scripts/package-linux.sh` sistem Qt kullanan küçük geliştirici paketini hazırlar. Erken erişim CI paketi `scripts/package-linux-portable.py` ile gerekli Qt kütüphanelerini, Wayland bileşenlerini ve uyumlu font yapılandırmasını içerir.

Dağıtılmış Linux paketini tek dosyalık AppImage olarak hazırlamak için `scripts/package-appimage.py` kullanılır. Önce portable arşivi açın; ardından resmi `appimagetool` aracını (veya FUSE gerektirmeyen çıkarılmış `AppRun` dosyasını) belirtin:

```sh
python3 scripts/package-appimage.py \
  --package dist/PhotoDeck-0.5.0-ea.1-linux-x86_64 \
  --appimagetool /path/to/appimagetool \
  --output dist/PhotoDeck-0.5.0-ea.1-x86_64.AppImage
```

AppImage'a çalıştırma izni verip açın: `chmod +x PhotoDeck-0.5.0-ea.1-x86_64.AppImage`. FUSE bulunmayan sistemlerde `APPIMAGE_EXTRACT_AND_RUN=1 ./PhotoDeck-0.5.0-ea.1-x86_64.AppImage` kullanılabilir. Windows taşınabilir paketindeki `PhotoDeck.exe`, yanındaki DLL ve eklenti klasörleriyle birlikte taşınmalıdır. macOS paketindeki `.app` tüm içeriğiyle korunmalıdır.

## Mevcut kapsam

RGB 8 bit raster düzenleme. PSD/PSB, RAW, CMYK/ICC, 16/32-bit, ayar katmanları, kanal mikseri/seçici renk, gruplar/bağlı veya gömülü akıllı nesneler, warp/puppet warp, vektör yolları/SVG, edge-aware seçim düzeltme, tablet basıncı, Actions/ABR/eklenti uyumluluğu ve Timeline henüz yoktur. Eğriler parça parça doğrusal; renk dengesi tüm tonlara uygulanan kanal kaydırması; feather kutu yumuşatmasıdır. Photoshop'un gelişmiş seçenekleriyle eşdeğer oldukları iddia edilmez. Yeniden boyutlandırma, tüm görüntü dönüş/aynalaması ve piksel araçları metni rasterleştirir; undo ile geri dönülebilir. Bulanıklık katmanın tamamına uygulanır; yerel sonuç için maske kullanılır. Kırpma dışarı taşan kaynak pikselleri saklar. Katman kilidi doğrudan düzenlemeyi engeller; tüm görüntü dönüşümleri bütün katmanları dönüştürür.

Bu ilk kullanılabilir sürüm Photoshop'un bütün profesyonel iş akışlarına eşdeğer değildir. Minimum donanım koşulları düşük özellikli gerçek cihazlarda ölçülmemiştir. Yerel ölçümler `artifacts/verification.md` dosyasında tutulur.

## Kaynak düzeni

`document` belge modeli, harmanlama, maske, filtre, geçmiş ve dosya biçiminin sahibidir. `canvas` koordinat dönüşümleri, zoom/pan ve araç etkileşimlerini yürütür. `mainwindow` belge sekmeleri, menüler, katman paneli, toplu işlem ve kurtarmayı birleştirir. `tests/test_photodeck.cpp` aynı çekirdek ve gerçek widget olaylarıyla davranışları doğrular.

Uygulama kodu ve örnek çizim MIT lisanslıdır. Qt ayrı LGPLv3/GPLv3 veya ticari lisanslı bağımlılıktır; Windows/macOS dağıtımlarında Qt lisans metinlerini ve kaynak erişimi bildirimini ekleyin.

İşlev doğrulama kapsamı: [docs/FEATURE-AUDIT.md](docs/FEATURE-AUDIT.md).

Tüm Photoshop alanlarının karşılaştırması ve açık farklar: [docs/PHOTOSHOP-COMPARISON.md](docs/PHOTOSHOP-COMPARISON.md). 0.3–0.5 farkların bir kısmını kapatır; karşılaştırma çalışması devam eder.
