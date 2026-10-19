# PhotoDeck 0.10.0 geliştirme notları

## Güncel aday — 0.10.0-ea.4

EA4, yayımlanmış EA3 kaynağı `580b1896f883acbf6c5591092d5045f60d017826` üzerine [PR #6](https://github.com/Meingless/PhotoDeck/pull/6) düzeltmelerini ve yeni katmanlı çerçeve simgesini ekler. Seçim yokken Ctrl+J bağımsız akıllı nesne kaynağı, çerçeve/dönüşüm/maske/efekt ve tek adım geri al/yineleyi korur; seçili piksel davranışı değişmez. Dar dönüşüm paneli kaynak yazı ölçülerine göre yerleşir; ayrıntı düğmesinin açık/kapalı ve erişilebilir durumu eşleşir. Kayıp dosya yolu boş canonical-path üzerinden başka sekme sanılmaz. Kilitli dışa aktarım oranı, eş boyut 1/16384 sınırına ulaşınca düzenlenen boyuta da geri yansır. Linux portable arşivi belgelenen kullanıcı menüsü kurucusunu ve simgesini içerir.

19 Ekim 2026 metadata öncesi PR başı `a4db6eed31ca38794b5b2a108ac1c72fafed2627` için [beş doğrulama işi](https://github.com/Meingless/PhotoDeck/actions/runs/39210256340) ve [dört paket işi](https://github.com/Meingless/PhotoDeck/actions/runs/39210256255) başarılıydı. Bunlar son EA4 metadata kaynağının kanıtı değildir. Her hedefte 24 grupta 1034 gerçek vaka vardı: Windows 1027 başarılı + 7 açık platform atlaması, diğer hedefler ve Linux sanitizer 1034 başarılı. init/cleanup bu sayıya dahil değildir. Yerel izinli 22 grup 1014 başarılı + aynı 7 platform atlaması ve 72 Python testi geçti; yerelde ayrıştırıcı/kurtarma-codec grupları bu tur tekrar çalıştırılmadı, uzak CI tam 24 grubu çalıştırdı.

Windows Qt offscreen eklentisinin fonts klasörü olmadan kutu glyph motoruna düşmesi, gerçek platform yazı ölçüsü farkından ayrıldı. CI artık var olan Windows Fonts klasörünü `QT_QPA_FONTDIR` ile kullanır; dönüşüm testinin başlangıcı iki gerçek farklı glyph gerektirir. Hiçbir test kapısı/izinli atlama gevşetilmedi. Önceki Linux masaüstü smoke yalnız o tarihteki ağaç için geçerlidir; EA4 Windows/macOS cihaz etkileşimi yapılmış sayılmaz.

Tek-sürüm yayımlayıcı yalnız PR #6, sabit EA3 atası, son PR ile aynı ağaçtaki güncel main ve her iki kaynağın dört platform/Linux ASan+UBSan doğrulaması için açılır. Yeni yayın paketleri 24 native CTest/QtTest grubu, Linux/Windows'ta ayrıca 24 paketlenmiş-runtime grubu, uygulama/AppImage açılışı, not eşitliği ve SHA-256 doğrulaması gerektirir. Son yayın kanıtı RELEASE-PROOF.json ve sürüm kaydında üretilir. [EA4 yayın kapıları ve sınırlar](releases/0.10.0-ea.4.md).

## Tarihsel EA3 geliştirme kaydı

EA3, yayımlanmış EA2 kaynağı `afeeeeb54f1195df4805b17e229f07ba5aa74f33` üzerine [gömülü akıllı nesneler](SMART-OBJECTS.md) ve [birleşik raster dışa aktarımı](EXPORT.md) ekler. Akıllı nesne kimliği, tam çözünürlüklü gömülü içerik, sabit çerçeve ve geri alınabilir değiştirme v7 proje/kurtarma biçiminde saklanır. EA2 v7'yi açamaz; akıllı nesne yoksa önceki v5/v6 normal kayıt seçimi korunur. Bağlantılı/iç içe belge, ayrı içerik sekmesi ve Photoshop akıllı filtre yığını bu aşamada yoktur. Maske/efekt işleme sabit çerçeve çözünürlüğündedir.

Test envanteri önceki 22 grubu koruyup `photodeck-smart-objects` ve `photodeck-export-dialog` ile **24** olur. Yeni gruplar kaynak koruması, içerik değiştirme/rasterleştirme/geri alma, kayıt-kurtarma, uyarı ve iptal akışları ile çıktı konum/biçim/üzerine yazma/önizleme tutarlılığını kapsar. Salt okunur UI ayrıntılarının paylaşılan katmanları gereksiz ayırmaması da akıllı nesne regresyonunda denetlenir.

17 Ekim 2026 sürüm metadata hazırlığı sırasında özellik kontrol noktası `5d5004e5b3726db0a2d9da8875492373f0709871` (yerel eşdeğer ağaç `ba288a6294bf3582d50181dda4e8c87cce9b4fae`) için [beş doğrulama işi](https://github.com/Meingless/PhotoDeck/actions/runs/39058690417) ve [dört paket işi](https://github.com/Meingless/PhotoDeck/actions/runs/39058690420) başarılıydı. Bu, son EA3 sürüm metadata commit'inin CI kanıtı değildir. Aynı özellik ağacı 17 Ekim 2026 tarihinde bulut Linux masaüstünde etkileşimli olarak da denetlendi: dönüştürme ve geri al/yinele, kaynak boyama koruması, %75/15° katman dönüşümü, içerik değiştirme/iptal, yeni v7 dosyasına kayıt ve yeniden açma, açık rasterleştirme/geri al, birleşik çıktı ve yerel klasör seçici iptali geçti. 2800×2000 yeni kaynak, 1400×1000 çerçeveyi ve 1210×998 dönüşmüş sınırı korudu; 700×500 PNG, 300 DPI ve ICC ile doğrulandı. Çıktı iptali ve üzerine yazmayı reddetme, proje/çıktı SHA-256 değerlerini korudu. Bu tek Linux etkileşimli smoke testi fiziksel cihaz matrisi veya son EA3 paket CI kanıtı değildir. Ek bozuk-girdi stres genişletmesi yapılmadı; önceki kayıtlı ayrıştırıcı/kurtarma regresyonları korunur.

Tek-sürüm yayımlayıcı yalnız incelenen PR #5, sabit EA2 atası, son PR ile aynı ağaçtaki güncel main kaynağı ve bunların başarılı dört platform/Linux ASan+UBSan doğrulaması için açılır. Paket kanıtı tam 24 native CTest/QtTest envanteri, Linux/Windows'ta 24 paketlenmiş-runtime grubu, dağıtılmış uygulama/AppImage açılışı, not eşitliği ve SHA-256 doğrulaması gerektirir. Önceki yayım etiketleri veya varlıkları değiştirilmez. Son kaynak ve gerçek yayın kanıtı yayımlayıcının ürettiği RELEASE-PROOF.json ve sürüm kaydında verilir. [EA3 yayın kapıları ve sınırlar](releases/0.10.0-ea.3.md).

## Tarihsel EA1/EA2 geliştirme kaydı

Bu kayıt önce `0.10.0-dev` olarak geliştirilen ve `0.10.0-ea.1` erken erişime hazırlanan kaynağı anlatır. 16 Ekim 2026 çalışmasının temel commit'i `e209345d91690ba091e535b13f2f6b5a4cb7aa4f` (yayımlanan 0.9.1-ea.1) idi. Aşağıdaki yerel geliştirme ölçümleri yeni sürümün uzak CI kanıtı değildir. Yayımlanan adayın tam commit ve dört-platform CI/paket sonuçları [sürüm kaydı](https://github.com/Meingless/PhotoDeck/releases/tag/v0.10.0-ea.1) ve RELEASE-PROOF.json içinde ayrıca verilir.

### Tamamlanan geliştirmeler

- Seçim işlemleri, seçimi kaldırma ve yeniden seçme artık belge geçmişinde korunur; piksel/katman geri alması seçimi silmez. Adlandırılmış seçimler 8 bit kapsamıyla saklanır ve geri alınabilir. [Davranış ve biçim kılavuzu](SELECTION-WORKFLOW.md).
- Ctrl+T tuval üstü taşıma, sekiz ölçek tutamacı ve ayrı döndürme tutamacını açar. Önizleme kaynak belgeyi değiştirmez; Enter uygular, Esc iptal eder, bütün sürüklemeler tek geçmiş adımıdır. Ctrl+Alt+T aynı önizlemenin sayısal/perspektif ayarlarına erişir. [Dönüşüm kılavuzu](ON-CANVAS-TRANSFORM.md).
- F2 ile katman adı satır içinde düzenlenir; Enter veya odak değişikliği uygular, Esc iptal eder. Sağ tık ve klavye bağlam menüsü açılan satırı hedefler. Piksel/metin/maske/kilit/kırpma durumu ikinci satırda görünür.
- Katman listesinde sürükleme, görünür bırakma çizgisi ve kenar kaydırmasıyla tek katmanı yeniden sıralar. İçerik sırasını değiştiren bırakma tek geri alma adımıdır; Esc veya liste dışına bırakma iptal eder. Dar kaydırılabilir denetçide ilk ve son konuma ulaşılır. [Katman kılavuzu](LAYER-WORKFLOW.md).
- Katmanlar X/Y koordinatlarıyla konumlandırılır; tuval veya seçim sınırına göre ayrı yatay/dikey hizalama komutları kaynak piksel, maske ve metni korur. Tuval odaklıyken Taşı aracında ok/Shift+ok 1/10 belge pikseli taşır; basılı tutulan hareket tek geri alma adımıdır. [Konumlandırma kılavuzu](LAYER-POSITIONING.md).
- Geçmiş paneli geri alınmış gelecek adımları da gösterir; fare veya klavyeyle kararlı geçmiş kimliğine güvenli geçiş yapılır. [Geçmiş kılavuzu](HISTORY-NAVIGATION.md).
- Seçimler ayrı Seçim menüsünden erişilir. Dönüşüm denetimlerinin yazı karşıtlığı açık ve özel temalarda iyileştirildi.
- Kurtarma kopyalarının görüntü kodlaması ve büyük yazımı arka planda yapılır. Tek anlık görüntü sınırı, belge kimliği/sürüm kontrolü ve atomik dosya değişimi kayıt/kapatma yarışlarını önler. Normal Kaydet işleminin baytları ve uyumluluk politikası korunur. [Kurtarma kılavuzu](RECOVERY.md).
- Dar Touch çalışma alanında dönüşüm çubuğu gerektiğinde tek satıra geçer; görünür tutamaçların tıklanabilir alanını korur. Renk sıfırlama ve metne çift tıklama bekleyen dönüşümü güvenle tamamlar. İşlem sırasında hedef/sekme değişimi veya pencere kapanışı eski önizlemeyi başka katmana uygulamaz; dar düzende Uygula/İptal düğmeleri hareketin hemen ardından da tam yüksekliktedir.
- Renk paneli dar alanda kaydırılır; alttaki katman/geçmiş panelinin üzerine çizilmez. Klavyeyle odaklanan denetim görünür alana getirilir. Geniş pencerenin doğal panel yüksekliği, kayıtlı düzen ve yüzen panel boyutları korunur.
- Yeni seçim ve katman konumu pencereleri Türkçe eylem düğmeleri, erişilebilir adlar ve klavye odak sırasıyla tamamlandı. Aynı isimli seçimin üzerine yazma sorusunda güvenli varsayılan İptal olarak kalır.
- Çakışan dışa aktar/birleştir kısayolları ayrıldı: Ctrl+Shift+E dışa aktar, Ctrl+Alt+Shift+E görünürleri birleştir, Ctrl+E alta birleştir.

### Proje uyumluluğu

Normal kayıt, adlandırılmış seçim yoksa **v5** olarak kalır. Yalnız etkin/geçici seçimler dosyayı v6'ya yükseltmez ve belgeyi değişmiş göstermez. Adlandırılmış seçimler **v6** gerektirir; yayımlanan 0.9.1 bunları açamaz. İlk v6 kaydında açık uyarı vardır ve eski özgün dosya ayrı kopyayla korunur. Son adlandırılmış seçim silinince normal kayıt yeniden v5 kullanır. Geçici kurtarma dosyaları seçim bağlamı için v6 kullanabilir; özgün projenin kimliğini veya kayıt noktasını değiştirmez.

### Doğrulama kaydı

16 Ekim 2026 tarihli birleşik üretim-kodu kontrol noktası `3839044f0dcfb61d69f7c91d521402b558eb8710`, Linux x86_64 / Qt 6.8.3 / GCC 14.2 / offscreen ortamında doğrulandı. Aşağıdaki sonuçlar bu kaynak içindir; daha sonraki üretim-kodu değişikliklerine otomatik olarak aktarılmaz. Bu kayıt sonradan eklenen belge güncellemesidir.

- Release: **21/21 CTest grubu**, **950 başarılı QtTest vakası**, 65,68 saniye.
- Debug + AddressSanitizer/UndefinedBehaviorSanitizer: **21/21 grup**, **950 başarılı vaka**, 136,92 saniye; sanitizer bulgusu yok. `halt_on_error=1`; Qt SDK instrument edilmedi, sızıntı denetimi kapalıydı. Bu bir sızıntı temizliği kanıtı değildir.
- Her iki koşuda yalnızca makine kimliği sunmayan ortam nedeniyle yedi gerçek makine-kimliği bağımlı kurtarma dosyası vakası atlandı. İsimleri ve gerekçeleri açık izin listesiyle denetlendi; 21 sentetik kimlik politikası vakasının tamamı çalıştı. Diğer gruplarda atlama yoktu.
- Gerçek Qt pencere olayları ve görüntüleriyle Focus/Paper/Touch, 900×620 ve 1280×800, 1×/1,5×/2× ölçeklerinde dönüşüm, konum, satır içi ad, kayıtlı seçim ve geçmiş akışları yeniden geçti. Katman sürükleme çizgisi/sonucu ve dar Touch listesinin ilk/son aralığa kayması 1×/2× ölçeklerinde ayrıca doğrulandı. Bunlar sanal offscreen ekran sonuçlarıdır.
- Renk panelinin önceki 41 odaklı düzen/yaşam döngüsü vakası her üç ölçekte geçti. Dar dönüşüm çubuğunun hareketten hemen sonraki düğme boyutu; on preset, büyük arayüz yazısı ve sıkışık dock yerleşimlerinde ayrıca sınandı.
- Envanter **21 CTest ve 21 qmake test projesidir**. Linux qmake yedeği artık tüm test projelerini keşfeder. Gerçek qmake uygulama derlemesi ve üç temsilî grubun 302 vakası daha önce geçti; tam yeni 21-grup qmake koşusu bu ilk kayıt sırasında henüz tamamlanmamıştı; son ek kontrol aşağıda kayıtlıdır.
- Yapı/paket envanteri yardımcı testleri **10/10**, mevcut tek-sürüm yayımlayıcının sahte servis kullanan yardımcı testleri **37/37** geçti. Üç iş akışı YAML dosyası ayrıştırıldı. Bu testler uzak yayımlama yapmaz.

#### Son yerel geliştirme paketi ve qmake doğrulaması

İnceleme paketinin kaydettiği `28a35e06b025f452830dbc4a69699e5dd1913782` kaynak kimliği için ek kontroller tamamlandı: çıkarılmış taşınabilir Linux paketinde ve değiştirilmemiş qmake derleme betiğinin bütün 21 test projesinde 950 başarılı vaka, aynı yedi makine-kimliği atlaması, sıfır hata. Paket koşusundaki 46 yükleyici izi SDK/sysroot'a dönmedi ve 15 fixture yolu paket içinden çözüldü. qmake koşusu yaklaşık 8 dakika 48 saniye sürdü; 129 kaynak dosyasının tamamı değişmeden kaldı. Varsayılan kurtarma zamanlayıcısı ayrıca 56/56 kontrolden geçti. Bu sonuçlar aşağıdaki tarihsel kontrol noktasını tamamlar; daha sonraki sürüm metadata/test-harness değişikliklerinin yeni uzak CI kanıtının yerine geçmez.

#### Önceki yerel paket kontrol noktası

`01a3c66dede8bef53e6705b9667c78a0e8c8dd56` kaynağının temiz Linux taşınabilir arşivinin çıkarılmış kopyasında **20/20 grup**: 799 geçen vaka, yukarıdaki 7 platform atlaması, 0 hata vardı. 15 test dosyası yolu paket içinden çözüldü; 45 yükleyici izinde SDK/sysroot kitaplığına dönüş görülmedi. Bu eski yerel paket kanıtı, sonradan eklenen katman sürükleme kodunu veya yeni bir yayımı doğrulamaz.

#### Kurtarma eşzamanlılığı ve ölçüm sınırı

`6df756b` kurtarma çekirdeğinin varsayılan ThreadSanitizer koşusunda yalnız yönetici testleri 27, dosya testleri 62 (ayrıca 7 platform atlaması), codec testleri 12 başarılı sonuç verdi. Tam GUI koşusu temiz ilan edilmedi: Qt çizim havuzu ve dosya penceresinde raporlar var. Aynı `QFileInfoGather` erişim deseni kurtarma yöneticisi belge kaydından önce durdurulmuşken de üretildi; benzer dosya penceresi raporu asenkron kurtarmadan önceki `35dddf5` kaynakta da vardı. Bu, uyarının yeni kurtarma iş parçacığını gerektirmediğini gösterir; Qt kusuru ile instrument edilmemiş SDK'nın görünmeyen senkronizasyonunu birbirinden ayırmaz. Hiçbir uyarı bastırılmadı.

Üretim kurtarma yöneticisinin ayrı sentetik ölçümünde üç adet 4000×4000 gürültü katmanının yeni/yenilenen kopyası 9,491/9,538 saniyede yayımlandı; 5 ms GUI kalp atımındaki en büyük boşluklar 6,890/16,320 ms idi. Önceki senkron iş yükünde GUI beklemesi saniyeler düzeyindeydi. Bu ölçüm `QApplication`, belge ve üretim yöneticisini kullanır; tam ana pencereyi içermez ve kodlayıcı hızlanması veya gerçek cihaz garantisi değildir. 30 gerçek yeni/yenilenen kopya yeniden açılıp kaynakla karşılaştırıldı; 606 yakalama denetimi piksel tamponu paylaşımını ve metaveri ayrılmasını doğruladı.

Bu sınır ayrıca gerçek 1440×960 ana pencereyle denetlendi: `01a3c66` ve yayımlanmış `e209345`, aynı 512²/4000² üç katmanlı kaynaklarla toplam sekiz yeni/yenilenen kurtarma kopyası üretti. 4000² iş yükünde eski sürümün GUI boşluğu 10.543,668/12.577,000 ms, yeni kodun 11,338/9,737 ms idi; yeni yayım 9,671/9,627 saniye sürdü. Her iki sürümde aynı sentetik 16 ms tuval çizimi ve 250 ms panel yenileme uyarımı kullanıldı; önbellekler önceden ısıtıldı. Yeni büyük-kopya koşularında 642/639 tuval çizimi ve 39/39 panel yenilemesi işlendi. Sekiz dosyanın tamamı yeniden açılıp piksel/metaveri ve canlı belge kimliğiyle karşılaştırıldı; aynı boyuttaki eski/yeni yenileme dosyaları bayt bakımından da eşitti. Bu tekil bulut/offscreen gözlemleri ölçülen kurtarma duraklamasının giderildiğini destekler; ilk çizim, soğuk önbellekle düzenleme veya tüm olası arayüz gecikmelerini ölçmez.

### Açık sınırlar

- Düzenleme ve çıktı RGB 8 bittir. PSD/RAW, CMYK/yüksek bit, ayar katmanları, gruplar, vektör yolları ve gelişmiş Photoshop uyumluluğu eklenmedi.
- Tutamaçlar etkin katmanın tamamını dönüştürür; seçili piksel parçasını, bağımsız maskeyi veya birden çok katmanı dönüştürmez. Kaynak geometrik sınırları kullanılır; saydam boşluk otomatik kırpılmaz.
- Etkin dönüşüm önizlemesi uygulanana kadar belge içeriği değildir; kurtarma son uygulanmamış hareketi garanti etmez.
- Fiziksel tablet/yazıcı, gerçek Windows/macOS cihazları ve düşük özellikli donanım bu yerel Linux çalışmasıyla doğrulanmış sayılmaz.
- Bu bulutta üretilen yerel geliştirme paketinin tüm bağımlılıkları birlikte değerlendirildiğinde glibc 2.39 gerekir (`libsystemd.so.0`). Ubuntu 22.04 veya daha eski Linux uyumluluğu iddia edilmez. AppImage, gerçek X11/Wayland ve yeni dört-platform CI bu kontrol noktasında çalıştırılmadı.
- Film düzeni, 900×620 pencere, 20 px arayüz yazısı ve elle açılmış Geçmiş paneli birlikte kullanıldığında tüm panel yığınının asgari yükseklikleri alanı 104 px aşar. Bu önceden var olan uç durumda paneller çakışabilir; aynı düzen 900×724'te çakışmasız doğrulandı. Renk düzeltmesi bütün olası özel panel düzenlerinin sığacağını garanti etmez.
- Ek bozuk-girdi stres genişletmesi çalıştırılmadı. Buradaki ayrıştırıcı kanıtı, mevcut kayıtlı regresyon testleriyle sınırlıdır.
