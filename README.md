# PhotoDeck 0.9.1 Early Access

AI içermeyen, tamamen yerel, C++/Qt 6 ile geliştirilen fotoğraf ve katman düzenleyici. Linux, Windows ve macOS için ortak kaynak kullanır. Tarayıcı motoru, Python çalışma zamanı, bulut hesabı, abonelik veya ağ servisi gerekmez.

## 0.9.1 erken erişim güncellemesi

Odak çalışma alanı, dosya çakışması/legacy kopya kaydı, daha erken kurtarma ve macOS Hakkında sürüm görünümü bu sürümde iyileştirildi. .pdeck v5 biçimi değişmedi. Ayrıntılar ve kalan sınırlar [sürüm notlarında](docs/releases/0.9.1-ea.1.md).

## Linux'ta kullan

Hazır uygulama için `./launch.sh` çalıştırın. `./scripts/install-user.sh` uygulama menüsüne kısayol ekler. Yerel geliştirici derlemesi `bin/photodeck`, x86_64 Linux ve sistem Qt kütüphanelerini kullanır. Qt uygulamayla birlikte taşınmadığından küçük paketin boyutu sistem Qt bağımlılıklarını içermez. Dağıtım paketleri ve AppImage ise Qt'yi içerir.

Sistem paketleri: CachyOS/Arch için `qt6-base qt6-imageformats`; Ubuntu/Debian'da kaynak derlemek için `build-essential cmake qt6-base-dev qt6-base-dev-tools qt6-image-formats-plugins`.

## Görünüm presetleri ve kişiselleştirme

Varsayılan görünüm **Odak**: solda araçlar, ortada panellerin kapatmadığı tuval, sağda katmanlar/özellikler ve üstte seçili araca göre değişen seçenekler bulunur. **Ctrl+Alt+1…0** on hazır çalışma alanı arasında geçiş yapar. Çalışma alanı menüsünde Grafit, Kağıt, Odak, Fotoğraf Masası, Çalışma Tezgahı, Şerit, Çizim Atölyesi, Kompakt, Dokunmatik ve Modüler presetleri bulunur. **Ctrl+Alt+P** görünüm düzenleyicisini açar; renkler, yazı ve simge boyutu, köşeler, panel yerleşimi ve görünürlük düzenlenebilir. Paneller taşınabilir/ayrılabilir, ayarlar oturumlar arasında korunur; kişisel presetler kaydedilebilir ve .pdui ile içe/dışa aktarılabilir. Erken erişim kurulum notları [EARLY-ACCESS.md](EARLY-ACCESS.md) içinde.

## Araçlar

- Aynı pencerede birden fazla belge, sürükleyerek dosya açma ve panodan görüntü katmanı ekleme.
- Piksel ve düzenlenebilir metin katmanları; taşıma, adlandırma, çoğaltma, sıralama, görünürlük ve kilit.
- Katman opaklığı ve 27 harmanlama modu: normal, çarp, ekran, kaplama, yakma/soldurma, ışık, fark, hue/saturation/color/luminosity dahil.
- Alt katmana kırpma maskesi (Ctrl+Alt+G): ardışık kırpılan katmanlar temel katmanın alfa/maskesini kullanır; temel görünürlük ve opaklık tüm zincire uygulanır. Katman listesinde ↳ işareti gösterilir.
- Ctrl+J seçim yoksa tüm katmanı, seçim varsa etkin katmanın seçimini yeni katmana kopyalar. Ctrl+Alt+J aynı seçim komutunu açar; Ctrl+Shift+J seçimi keser. Maskeler/renk ayarları/dönüşüm kopya piksellere uygulanır; kesmede kaynak katman da rasterleştirilir. Kesme kilitli veya kırpma zinciri içindeki katmanlarda reddedilir; ilişkiyi kaldırıp işlemi tekrar yapabilirsiniz.
- Ctrl+E alta birleştirir; işlem önce/sonra görünümü karşılaştırır. Görünümü değiştirecek bağımlı harmanlama/kırpma zincirleri reddedilir; görünür katmanları birleştirme alternatifi vardır.
- Beyazın gösterip siyahın gizlediği katman maskeleri. Seçimden maske oluşturma, maskeyi ters çevirme veya kaldırma.
- Fırça, kalem, silgi, aynı katmandan klon damgası, bağlı alana boya kovası, iki renkli degrade, dikdörtgen, elips ve damlalık.
- Ctrl+Alt+B fırça/tablet ayarları: uç açısı, yuvarlaklık, çap yüzdesiyle darbe aralığı; basınçla boyut/akış, minimum boyut ve tepki eğrisi; kalem eğimiyle açı/yuvarlaklık. Qt tablet girdisi ve kalemin silgi ucu aynı seçim/maske/geri alma akışını kullanır. Fare tam basınçta çalışır.
- Düzenle → Fırça kütüphanesi veya Ctrl+Alt+Shift+B: bütün uç/tablet değerlerini isimli preset olarak kaydetme, yeniden seçme, silme, .pdbrush ile içe/dışa aktarma ve sonraki açılışta saklama. Görselden veya görünür kompozisyonun seçiminden özel uç oluşturma: koyu alanlar boyar, beyaz/saydam alanlar gizlenir; oran korunarak en uzun kenar 512 px'e sığdırılır.
- Dikdörtgen/eliptik seçim, serbest/çokgen kement ve toleranslı sihirli değnek. Yeni/ekle/çıkar/kesiştir; ters çevirme, kenar yumuşatma ve büyütme/küçültme. Seçim alfası çizim, silme, kopyalama ve maske oluşturma işlemlerinde korunur.
- Seçili piksel silme, kırpma, oranı koruyan yeniden boyutlandırma, 90° dönüş ve aynalama.
- Ctrl+T ile kaynak koruyan katman dönüşümü: ölçek, keyfi açı, eğme, aynalama ve dört köşeli perspektif. Kaynak piksel, metin ve maske korunur; önizleme iptal edilebilir ve tek adımda geri alınır.
- Dokuz sabitleme noktasıyla tuval boyutu; tuval dışındaki kaynak pikselleri silmez. Görünür katmanları birleştirme tuval dışındaki alanları ve gizli katmanları korur. Tuvale düzleştirme saydamlığı koruyup gizli katmanları ve tuval dışını kaldırır.
- Kaynak pikselleri koruyan parlaklık, kontrast, doygunluk, siyah beyaz ve kutu bulanıklığı.
- Kaynak koruyan Gaussian, unsharp keskinleştirme (miktar/yarıçap/eşik), high-pass, tekrarlanabilir tek renkli/renkli gren ve pikselleştirme. Canlı önizleme kapatılabilir; kabul tek geri alma adımı, iptal tam geri dönüş sağlar.
- Maske etkinleştirme, yoğunluk ve Gaussian kenar yumuşatma; orijinal maske pikselleri korunur. Katman → Maske yoğunluğu / kenar yumuşatma penceresinden ayarlanır.
- Seviyeler (giriş/çıkış aralığı, gamma), pozlama, RGB ve ayrı kanal eğrileri; hue, vibrance, temel kanal renk dengesi, invert, threshold ve posterize. Ctrl+L ile canlı önizleme; iptal geri döner, kabul tek geri alma adımıdır.
- Yerel fontlarla çok satırlı metin, boyut, kalınlık ve renk. Metin katmanına çift tıklayarak düzenleme.
- PNG/JPEG/WebP/BMP ve TIFF içe/dışa aktarma; GIF ilk kare içe aktarımı. WebP/TIFF sistemin Qt görüntü eklentisine bağlıdır. Çalışma belgesi RGB 8 bittir; yüksek bit derinlikli girdiler 8 bite dönüşür. Çok sayfalı TIFF/animasyonlu GIF yalnızca ilk görüntüyü açar.
- Saydam PNG/WebP; JPEG saydam alanları beyaz arka plan üzerine birleştirir.
- Çıktı boyutu, oran kilidi, DPI ve kalite seçimi; sRGB, Display P3 veya Adobe RGB ICC profilli PNG/JPEG/TIFF/WebP çıktı. Profilli RGB girdiler sRGB çalışma alanına dönüştürülür; etiketsiz girdiler sRGB kabul edilir.
- A4, Letter veya görüntünün fiziksel boyutunda tek sayfalı PDF; yatay/dikey sayfa ve kenar boşlukları. Dosya → Yazdır native yazıcı penceresini açar; şeffaflık beyaz kağıda uygulanır.
- Orijinalleri değiştirmeyen toplu boyutlandırma, renk ayarı ve biçim dönüşümü. Çıktı adlarında `_photodeck` eki kullanılır; mevcut çıktılar için sayı eklenir.
- Atomik `.pdeck` proje kaydı; katman, maske, metin, konum, harmanlama ve renk/filtre parametreleri korunur. Diskte dışarıdan değiştirilen veya silinen açık proje için iptal, ayrı kopya ve açıkça üzerine yazma seçenekleri sunulur.
- Tamamlanan düzenlemelerden yaklaşık 5 saniye sonra kurtarma kopyası; ayrıca 45 saniyelik periyodik kontrol. Süren çizim/taşıma hareketi sırasında yazma ertelenir, hareket tamamlandığında hemen yeniden denenir. Sonraki açılışta kurtarma önerisi, normal kapatmada kaydet/at/iptal seçimi vardır.

Kısayollar: V taşı, M seç, C kırp, B fırça, P kalem, E silgi, S klon, F kova, G degrade, T metin, U dikdörtgen, O elips, I damlalık, H el, X renk değiştir. Klon kaynağı Alt+tıklama. Kırpmayı Ctrl+Shift+X ile uygula. Ctrl/Cmd+Z geri alır; Ctrl/Cmd+0 sığdırır; Ctrl/Cmd+1 gerçek boyutu gösterir. Tekerlek yakınlaştırır, orta tuş tuvali kaydırır. Bazı özel kısayollar platformdan bağımsız Ctrl olarak tanımlanmıştır; macOS'ta standart Dosya/Düzenle kısayolları Cmd kullanır.

Shift+M eliptik seçim, L kement, Shift+L çokgen kement, W sihirli değnek. Çokgen kement Enter/çift tıklamayla tamamlanır, Esc iptal eder. Seçim sırasında Shift ekler, Alt çıkarır, Shift+Alt kesiştirir; araç seçeneklerinden aynı modlar seçilebilir. Ctrl+Shift+I seçimi ters çevirir. Seçimler bu sürümde geçicidir; proje veya belge geçmişine kaydedilmez.

0.9 projeleri `.pdeck` sürüm 5 olarak kaydedilir; eski sürüm 1/2/3/4 projeleri açılabilir. Eski uygulamalar sürüm 5 dosyalarını açamaz. v1–v4 orijinalinin üzerine kaydetmek isteyen arayüz işlemi uyarı verir ve farklı adla v5 kopya kaydetmeye yönlendirir; eski orijinal korunur. v2/v3/v4 uyumluluk örnekleri yayımlanmış 0.3/0.4/0.5 AppImage'larıyla oluşturuldu.

Kırpma zinciri katman sırasına bağlıdır. Temel katmanı silmek çocukların kırpmasını kaldırır; sıralama/yeni katman yerleştirme hangi temel katmana kırpıldığını değiştirebilir. Gizli temelli gruplar görünür birleştirmede korunur. Görünür temeli ve gizli kırpılan çocuğu olan gruplar birleştirilmeden önce ayrılmalı veya görünür yapılmalıdır; ilişkiler sessizce kaybedilmez. Feather ile kesme, kısmi alfa sınırlarında iki ayrı katman oluşturduğu için sert seçim gibi piksel piksel aynı görünümü garanti etmez.

Dönüşmüş katmanda piksel boyamak/silmek için Katman → Görünümü rasterleştir kullanın. Bu açık adım dönüşümü, maskeyi ve renk ayarlarını kaynak piksellere uygular; metin düzenlenebilirliği kaldırılır, görünür sonuç ve geri alma korunur. Tüm görüntüye boyutlandırma/90° dönüş/aynalama uygulanması da dönüştürülmüş katmanları önce rasterleştirir. Serbest dönüşüm şu an sayısal bir pencereyle yapılır; tuval üstü tutamaçlar ve warp henüz yoktur.

Açılıştaki Horizon kompozisyonu PhotoDeck için kodla çizilmiş özgün bir örnektir. `examples/Horizon.pdeck` dosyası dört ayrı düzenlenebilir katman içerir.

## Hafiflik ve güvenlik sınırları

Tuval en fazla 16 megapiksel, her kenar en fazla 16384 piksel. En fazla 64 katman ve 256 MiB kaynak piksel/maske verisi; katmanların toplam dönüşmüş görünüm alanı da 256 MiB ile sınırlıdır. Geçmiş en fazla 30 durum ve canlı belgeden ayrı toplam 128 MiB benzersiz piksel verisi saklar; büyük belgelerde eski adımlar daha erken atılır. Paylaşılan görüntü tamponları gereksiz kopyaları azaltır; filtre sonuçları önbelleğe alınır. Değişmeyen katman/filmşeridi küçük resimleri yeniden kullanılır, canlı hareket sırasında yardımcı önizleme yenilemesi ertelenir. Bu sınırlar toplam süreç RAM'inin üst sınırı değildir: tuval önizlemesi, filtre/dönüşüm tamponları, Qt ve dosya kodlama ek bellek kullanır.

Bozuk proje okumaları geçici belge üzerinde doğrulanır ve açık belgeyi değiştirmez. Uzunluk, boyut, katman sayısı, maske boyutu ve sayısal parametreler sınırlanır. Proje dosyalarında komut, betik veya harici kaynak çalıştırılmaz. Başarısız kayıt açık belgenin kaydedilmiş durumunu değiştirmez. Proje/geçmiş ayrıdır: undo geçmişi dosyaya yazılmaz. Kurtarma sıfır veri kaybı garantisi değildir: henüz tamamlanmamış hareket, yazma öncesi kapanma veya disk hatası son değişiklikleri kaybettirebilir. PNG kodlama ve dosya yazma hâlâ arayüz iş parçacığında eşzamanlıdır; büyük belgelerde bekleme riski sürer. Dış değişiklik kontrolü dosya kimliği, boyut/zaman ve SHA-256 özetiyle iyimser doğrulama yapar; kontrol ile atomik kayıt arasındaki çok dar yarış aralığını bütünüyle kaldırmaz.

## Linux / Windows / macOS derleme

Qt **6.5 veya sonrası**, C++17 derleyicisi ve CMake 3.21+ gerekir. Testler için Qt Test modülü gerekir. CMake'ye Qt yolunu gerekirse `-DCMAKE_PREFIX_PATH=/Qt/6.8.3/...` ile verin.

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --config Release --parallel 4
ctest --test-dir build-cmake -C Release --output-on-failure
```

Linux için `./scripts/build.sh` CMake varsa onu, yoksa qmake6 + make kullanır ve testleri çalıştırır. Alternatif: ayrı bir build klasöründe `qmake6 ../PhotoDeck.pro && make -j4`.

Windows'ta Qt MSVC veya MinGW kitiyle derleyin; `windeployqt` Qt DLL'lerini ve eklentilerini uygulamanın yanına koyar. MSVC/CMake çıktısı `build-cmake/Release/photodeck.exe` olur. macOS'ta çıktı `build-cmake/photodeck.app`; `macdeployqt` bağımlılıkları `.app` içine yerleştirir. Dağıtım için Windows imzalama ve macOS imzalama/notarization ayrıca gerekir.

`.github/workflows/validate.yml` her pull request ve `main` push'unda Qt 6.8.3 ile Linux x86_64, Windows x64, macOS Intel ve Apple Silicon hedeflerinde CMake derlemesi ve 14 CTest grubunu çalıştıracak şekilde tanımlıdır. Test raporları ve CTest günlükleri iş artifacts bölümüne yüklenir. Bu dört hedefin CI kapsamı, tüm işletim sistemi sürümleri veya fiziksel cihazlar için uyumluluk garantisi değildir. Yeni akışın eklenmesi başarılı bir run kanıtı sayılmaz; ilk GitHub run sonucu ayrıca kontrol edilmelidir.

Ayrı Linux işi uygulama ve test kodunu Clang AddressSanitizer/UndefinedBehaviorSanitizer ile derler; hata ilk bulguda işi durdurur. Küçük, tekrarlanabilir `.pdeck`/`.pdbrush` kesme ve byte mutasyonu corpus'u da bu işte çalışır. Bu bir kapsam güdümlü fuzzing altyapısı veya güvenlik sertifikası değildir. Önceden derlenmiş Qt SDK'sı instrument edilmez; süreç sonu leak detection kapalıdır. Aynı kontrolü Linux'ta Qt yolu tanımlı ortamda çalıştırmak için:

```sh
cmake -S . -B build-sanitizers -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build-sanitizers --parallel 4
QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  ctest --test-dir build-sanitizers -C Debug --output-on-failure --timeout 120
```

`.github/workflows/build.yml` etiket (`v*`) veya manuel çalıştırmayla erken erişim paketlerini hazırlar; PR doğrulama akışından ayrıdır. Linux x86_64, Windows x64 ve macOS Intel/Apple Silicon için paketler ve açılış görüntüleri bu akıştadır. Linux/Windows paket denetimleri CTest envanterinden bulunan 14 grubun tümünü paketlenmiş Qt ile çalıştırır; legacy proje fixture'ları test için eklenir, ardından test araçlarıyla birlikte kaldırılır. Paketler önce Actions artifacts olarak hazırlanır; GitHub Release yayımlaması ayrı adımdır. Windows yayıncı sertifikası ve macOS Developer ID/noter onayı yoktur; macOS ad hoc imza denetimi yayıncı kimliğini doğrulamaz.

Linux paketleme: `./scripts/package-linux.sh` sistem Qt kullanan küçük geliştirici paketini hazırlar. Erken erişim CI paketi `scripts/package-linux-portable.py` ile gerekli Qt kütüphanelerini, Wayland bileşenlerini ve uyumlu font yapılandırmasını içerir.

Dağıtılmış Linux paketini tek dosyalık AppImage olarak hazırlamak için `scripts/package-appimage.py` kullanılır. Önce portable arşivi açın; ardından resmi `appimagetool` aracını (veya FUSE gerektirmeyen çıkarılmış `AppRun` dosyasını) belirtin:

```sh
python3 scripts/package-appimage.py \
  --package dist/PhotoDeck-0.9.1-ea.1-linux-x86_64 \
  --appimagetool /path/to/appimagetool \
  --output dist/PhotoDeck-0.9.1-ea.1-x86_64.AppImage
```

AppImage'a çalıştırma izni verip açın: `chmod +x PhotoDeck-0.9.1-ea.1-x86_64.AppImage`. FUSE bulunmayan sistemlerde `APPIMAGE_EXTRACT_AND_RUN=1 ./PhotoDeck-0.9.1-ea.1-x86_64.AppImage` kullanılabilir. Windows taşınabilir paketindeki `PhotoDeck.exe`, yanındaki DLL ve eklenti klasörleriyle birlikte taşınmalıdır. macOS paketindeki `.app` tüm içeriğiyle korunmalıdır.

## Mevcut kapsam

RGB 8 bit raster düzenleme. PSD/PSB, RAW, CMYK/proofing, 16/32-bit, ayar katmanları, kanal mikseri/seçici renk, gruplar/bağlı veya gömülü akıllı nesneler, warp/puppet warp, vektör yolları/SVG, edge-aware seçim düzeltme, fiziksel tablet cihaz matrisi, Actions/ABR/eklenti uyumluluğu ve Timeline henüz yoktur. Eğriler parça parça doğrusal; renk dengesi tüm tonlara uygulanan kanal kaydırması; feather kutu yumuşatmasıdır. Photoshop'un gelişmiş seçenekleriyle eşdeğer oldukları iddia edilmez. Yeniden boyutlandırma, tüm görüntü dönüş/aynalaması ve piksel araçları metni rasterleştirir; undo ile geri dönülebilir. Bulanıklık katmanın tamamına uygulanır; yerel sonuç için maske kullanılır. Kırpma dışarı taşan kaynak pikselleri saklar. Katman kilidi doğrudan düzenlemeyi engeller; tüm görüntü dönüşümleri bütün katmanları dönüştürür.

Bu ilk kullanılabilir sürüm Photoshop'un bütün profesyonel iş akışlarına eşdeğer değildir. Minimum donanım koşulları düşük özellikli gerçek cihazlarda ölçülmemiştir. Yerel ölçümler `artifacts/verification.md` dosyasında tutulur.

## Kaynak düzeni

`document` belge modeli, harmanlama, maske, filtre, geçmiş ve dosya biçiminin sahibidir. `canvas` koordinat dönüşümleri, zoom/pan ve araç etkileşimlerini yürütür. `mainwindow` belge sekmeleri, menüler, katman paneli, toplu işlem ve kurtarmayı birleştirir. `tests/test_photodeck.cpp` aynı çekirdek ve gerçek widget olaylarıyla davranışları doğrular.

Uygulama kodu ve örnek çizim MIT lisanslıdır. Qt ayrı LGPLv3/GPLv3 veya ticari lisanslı bağımlılıktır; Windows/macOS dağıtımlarında Qt lisans metinlerini ve kaynak erişimi bildirimini ekleyin.

İşlev doğrulama kapsamı: [docs/FEATURE-AUDIT.md](docs/FEATURE-AUDIT.md).

Filtre sırası pikselleştirme → gren → Gaussian → high-pass → unsharp; eski kutu bulanıklığı ve ton ayarları bunlardan önce, maske ve katman dönüşümü sonra uygulanır. Gaussian ayrılabilir, normalize edilmiş gerçek çekirdek kullanır; yarıçap 0–16 px, sigma `max(0.5, yarıçap/2)` ve sınır piksel tekrarları vardır. Maske feather aynı Gaussian yöntemidir. Filtreler RGB 8 bit ve katman ölçeğindedir; reorderable Smart Filter stack, filter mask, lens/motion blur, adaptive/smart sharpen veya Photoshop preset uyumluluğu değildir. Büyük görüntü ve yarıçap CPU/bellek maliyetini artırır; canlı önizleme kapatılabilir.

Tüm Photoshop alanlarının karşılaştırması ve açık farklar: [docs/PHOTOSHOP-COMPARISON.md](docs/PHOTOSHOP-COMPARISON.md). 0.3–0.9 farkların bir kısmını kapatır; karşılaştırma çalışması devam eder.

Çıktı kapsamı (0.9): Çalışma alanı RGB/sRGB 8 bittir. ICC çıktı pikselleri seçilen RGB profiline dönüştürülür; BMP profil eklemeyi desteklemez. DPI metadata desteği biçime bağlıdır; mevcut piksel ayrıntısını artırmaz. PDF görünür kompozisyonun düzleştirilmiş raster çıktısıdır; katmanlı/vektör/PDF-X değildir. Fiziksel yazıcı testi yapılmadı. CMYK/Lab, yüksek bit derinliği, monitör profiliyle görüntüleme, soft proof ve gamut uyarısı açık hedeflerdir.

Fırça kapsamı (0.9): Darbe aralığı çizgi boyunca korunur; bırakma noktasına son darbe uygulanır. Alfa seçim kapsamı her darbeye ayrı uygulanır. Basınç örnekleri hareket boyunca doğrusal enterpole edilir; boyut minimum + (1−minimum) × basınç^tepki, akış basınç^tepki kullanır. Eğim, eliptik ucu yönlendirir ve sıkıştırır. Ayarlar belge sekmesinin oturumu içindir; proje kaydına eklenmez. Özel görüntü uçları ve kalıcı fırça kütüphanesi 0.9 kapsamındadır. ABR, scatter/jitter/texture, smoothing/simetri ve fiziksel tablet cihaz matrisi açık hedeflerdir. Fiziksel tablet testi yapılmadı; testler gerçek Qt tablet olaylarını aygıt fixture'larıyla gönderir.

0.9: Menüler, sekmeler, araçlar, paneller, listeler, girişler, odak/hover/seçim durumları ve diyaloglar ortak modern tasarımla yenilendi. Varsayılan **3 · Odak**, on yerleşim ve .pdui özelleştirmeleri korunur. Araç ikonları farklı çözünürlüklerde çizilir; özel panel renklerinde okunabilir metin rengi türetilir. Renk seçiminiz değiştirilmez.

Fırça kütüphanesi uygulama yapılandırma klasöründeki `brushes.pdbrush` dosyasındadır. 128 kişisel preset, toplam 8 MiB gri uç maskesi ve 16 MiB dosya sınırı vardır. İçe aktarım mevcut isimleri değiştirmez, çakışan isimlere sayı ekler; geçersiz koleksiyon bütünüyle reddedilir. Kayıt atomiktir, ortak dosya kilidi ve oturum değişikliği kontrolü vardır. Okunamayan dosya ilk değişiklikten önce ayrı bir yedeğe kopyalanır. `--no-settings` kullanıcı kütüphanesini okumaz/yazmaz; açıkça istenen dosya dışa aktarımı çalışır.

Görsel uçlar basınç/eğim, seçim, piksel/maske ve silgiyle çalışır. Klon kaynağı dönmez; yalnızca uç kapsamı dönüp ölçeklenir. Özel uçta sertlik yerine maskenin kendi kenarları kullanılır. Preset araç ve fırça değerlerini saklar; ön plan rengi, klon kaynak konumu ve belge verileri saklanmaz. .pdeck v5 değişmedi. ABR, scatter/jitter/texture, smoothing/simetri ve fiziksel tablet cihaz matrisi açık kalır.
