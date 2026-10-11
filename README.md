# PhotoDeck 0.2.0 Early Access

AI içermeyen, tamamen yerel, C++/Qt 6 ile geliştirilen fotoğraf ve katman düzenleyici. Linux, Windows ve macOS için ortak kaynak kullanır. Tarayıcı motoru, Python çalışma zamanı, bulut hesabı, abonelik veya ağ servisi gerekmez.

## Linux'ta kullan

Hazır uygulama için `./launch.sh` çalıştırın. `./scripts/install-user.sh` uygulama menüsüne kısayol ekler. Bu oturumda derlenmiş `bin/photodeck`, x86_64 Linux ve Qt 6.11.2 sistem kütüphanelerini kullanır. Qt uygulamayla birlikte taşınmadığından küçük paketin boyutu sistem Qt bağımlılıklarını içermez. Eski Linux dağıtımlarında bu ikili uyumlu olmayabilir; kaynak kodu o sistemde derleyin.

Sistem paketleri: CachyOS/Arch için `qt6-base qt6-imageformats`; Ubuntu/Debian'da kaynak derlemek için `build-essential cmake qt6-base-dev qt6-base-dev-tools qt6-image-formats-plugins`.

## Görünüm presetleri ve kişiselleştirme

Varsayılan görünüm **Odak**. Çalışma alanı menüsünde Grafit, Kağıt, Odak, Fotoğraf Masası, Çalışma Tezgahı, Şerit, Çizim Atölyesi, Kompakt, Dokunmatik ve Modüler presetleri bulunur. **Ctrl+Alt+P** görünüm düzenleyicisini açar; renkler, yazı ve simge boyutu, köşeler, panel yerleşimi ve görünürlük düzenlenebilir. Paneller taşınabilir/ayrılabilir, ayarlar oturumlar arasında korunur; kişisel presetler kaydedilebilir ve .pdui ile içe/dışa aktarılabilir. Erken erişim kurulum notları [EARLY-ACCESS.md](EARLY-ACCESS.md) içinde.

## Araçlar

- Aynı pencerede birden fazla belge, sürükleyerek dosya açma ve panodan görüntü katmanı ekleme.
- Piksel ve düzenlenebilir metin katmanları; taşıma, adlandırma, çoğaltma, sıralama, görünürlük ve kilit.
- Katman opaklığı; normal, çarp, ekran, kaplama, koyulaştır ve açıklaştır harmanlamaları.
- Beyazın gösterip siyahın gizlediği katman maskeleri. Seçimden maske oluşturma, maskeyi ters çevirme veya kaldırma.
- Fırça, kalem, silgi, aynı katmandan klon damgası, bağlı alana boya kovası, iki renkli degrade, dikdörtgen, elips ve damlalık.
- Dikdörtgen seçim, seçili piksel silme, kırpma, oranı koruyan yeniden boyutlandırma, 90° dönüş ve aynalama.
- Kaynak pikselleri koruyan parlaklık, kontrast, doygunluk, siyah beyaz ve kutu bulanıklığı.
- Yerel fontlarla çok satırlı metin, boyut, kalınlık ve renk. Metin katmanına çift tıklayarak düzenleme.
- PNG/JPEG/WebP/BMP içe ve dışa aktarma. WebP sistemin Qt görüntü eklentisine bağlıdır.
- Saydam PNG/WebP; JPEG saydam alanları beyaz arka plan üzerine birleştirir.
- Orijinalleri değiştirmeyen toplu boyutlandırma, renk ayarı ve biçim dönüşümü. Çıktı adlarında `_photodeck` eki kullanılır; mevcut çıktılar için sayı eklenir.
- Atomik `.pdeck` proje kaydı; katman, maske, metin, konum, harmanlama ve renk/filtre parametreleri korunur.
- 45 saniyelik kurtarma kopyaları; sonraki açılışta kurtarma önerisi. Normal kapatmada kaydet/at/iptal seçimi.

Kısayollar: V taşı, M seç, C kırp, B fırça, P kalem, E silgi, S klon, F kova, G degrade, T metin, U dikdörtgen, O elips, I damlalık, H el, X renk değiştir. Klon kaynağı Alt+tıklama. Kırpmayı Ctrl+Shift+X ile uygula. Ctrl/Cmd+Z geri alır; Ctrl/Cmd+0 sığdırır; Ctrl/Cmd+1 gerçek boyutu gösterir. Tekerlek yakınlaştırır, orta tuş tuvali kaydırır. Bazı özel kısayollar platformdan bağımsız Ctrl olarak tanımlanmıştır; macOS'ta standart Dosya/Düzenle kısayolları Cmd kullanır.

Açılıştaki Horizon kompozisyonu PhotoDeck için kodla çizilmiş özgün bir örnektir. `examples/Horizon.pdeck` dosyası dört ayrı düzenlenebilir katman içerir.

## Hafiflik ve güvenlik sınırları

Tuval en fazla 16 megapiksel, her kenar en fazla 16384 piksel. En fazla 64 katman ve 256 MiB kaynak piksel/maske verisi. Geçmiş en fazla 30 durum ve toplam 128 MiB benzersiz piksel verisi saklar; büyük belgelerde eski adımlar daha erken atılır. Paylaşılan görüntü tamponları gereksiz kopyaları azaltır; filtre sonuçları önbelleğe alınır. Bu sınırlar toplam süreç RAM'inin üst sınırı değildir: tuval önizlemesi, filtre tamponları, Qt ve dosya kodlama ek bellek kullanır.

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

Linux paketleme: `./scripts/package-linux.sh`. Bu küçük paket sistem Qt kullanır. CI Linux çıktısı da kullanılan Qt sürümünün sistemde mevcut olmasını gerektirir.

## Mevcut kapsam

RGB 8 bit raster düzenleme. PSD/PSB katman uyumluluğu, RAW geliştirme, CMYK/ICC baskı yönetimi, eğriler/seviyeler, vektör pen/path/SVG export, gelişmiş seçimler, basınç duyarlı tablet fırçası, Photoshop action/brush/eklenti uyumluluğu ve video/3D henüz yoktur. Yeniden boyutlandırma, dönüş/aynalama ve piksel araçları metin katmanını rasterleştirir; undo ile geri dönülebilir. Bulanıklık katmanın tamamına uygulanır; yerel sonuç için maske kullanılır. Kırpma kaynak katmanların dışarı taşan piksellerini saklar, yalnızca tuval sınırını değiştirir. Katman kilidi doğrudan çizim/maske/metin/renk düzenlemesini engeller; tüm görüntü dönüşümleri bütün katmanları dönüştürür.

Bu ilk kullanılabilir sürüm Photoshop'un bütün profesyonel iş akışlarına eşdeğer değildir. Minimum donanım koşulları düşük özellikli gerçek cihazlarda ölçülmemiştir. Yerel ölçümler `artifacts/verification.md` dosyasında tutulur.

## Kaynak düzeni

`document` belge modeli, harmanlama, maske, filtre, geçmiş ve dosya biçiminin sahibidir. `canvas` koordinat dönüşümleri, zoom/pan ve araç etkileşimlerini yürütür. `mainwindow` belge sekmeleri, menüler, katman paneli, toplu işlem ve kurtarmayı birleştirir. `tests/test_photodeck.cpp` aynı çekirdek ve gerçek widget olaylarıyla davranışları doğrular.

Uygulama kodu ve örnek çizim MIT lisanslıdır. Qt ayrı LGPLv3/GPLv3 veya ticari lisanslı bağımlılıktır; Windows/macOS dağıtımlarında Qt lisans metinlerini ve kaynak erişimi bildirimini ekleyin.
