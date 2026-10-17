# Tuval üstü dönüşüm

Ctrl+T veya Katman menüsündeki katman dönüştürme komutu, etkin katmanın etrafında mint renkli bir dönüşüm kutusu açar. Kaynak piksel, düzenlenebilir metin ve maske birlikte korunur. Önizleme sırasında gerçek belge ve geri alma geçmişi değişmez.

## Önce hedefi seçin

Taşı aracında **Tuvalden katman seç** yeni belge tuvalleri için açıktır. Tıklama, görünür ve kilidi açık katmanlar arasında imlecin altındaki en üst görünür pikseli hedefler; saydam/maskelenmiş alanlar ve görünmeyen kırpma alanları atlanır. Boş yere tıklama başka katmanı sürüklemez. Yalnız tıklamak bir düzenleme veya geri alma adımı oluşturmaz. Sabit bir hedefi taşımak istediğinizde bu seçeneği kapatın.

Etkin katmanın adı ve tuvalden seçimin açık/kapalı durumu tuvalde görünür. **Katman sınırlarını göster** görünüm sınırını açıp kapatır. Geri alma, dönüşümün başladığı hedef katmana döner; sonraki Ctrl+T görünür etkin satırla eşleşir.

## Boyutlandırın veya döndürün

- Kutunun içini sürükleyerek taşıyın. Shift hareketi yatay/dikey eksene sabitler.
- Köşeleri sürüklemek oranı korur; Shift serbest yatay/dikey ölçeklemeye geçer.
- Kenar ortasındaki tutamaç tek ekseni ölçekler; Shift iki eksenin oranını korur.
- Alt ile merkezden ölçekleyin. Bir tutamaç karşı kenarı geçtiğinde katman ters dönmez; küçük pozitif boyutta durur.
- Üstteki yuvarlak tutamaç katman merkezinde döndürür. Shift açıyı 15° adımlara sabitler.
- Ok tuşları 1 belge pikseli, Shift+ok 10 piksel taşır.
- Tekerlek yakınlaştırır; orta fare tuşu tuvali kaydırır. Tutamaçların ekran boyutu yakınlaştırmayla değişmez.
- Enter veya Uygula bütün sürüklemeleri tek geri alma adımında kabul eder. Esc veya İptal önizlemeyi hiçbir belge değişikliği olmadan kapatır. Kutunun içinde çift tıklamak da uygular.

Sayısal düğmesi, aynı önizleme üzerinde ölçek/açı/eğme/aynalama/perspektif ayarlarını açar. Oran kilidi açıkken yatay veya dikey ölçeğe 150 yazmak iki alanı da %150 yapar. Kilidi kapatınca eksenler bağımsızdır. Sayısal ölçek ve açı, pencerenin açıldığı önizlemeye göre değişimdir; kaynak dosyanın ilk boyutuna göre mutlak bir giriş değildir. Sabitleme noktası varsayılan olarak merkezdir; seçenekler açılıştaki görünümün eksene hizalı sınırına göredir.

Sayısal oran kilidi yalnız iki sayısal ölçek alanını bağlar. Farede köşe/kenar ve Shift/Alt davranışları yukarıdaki gibi korunur.

Pencereyi iptal etmek önceki tuval önizlemesine döner; kabul etmek tuval önizlemesini günceller. Sonucu tuvalde Uygula ile kaydedin. Ctrl+Alt+T veya Katman → Sayısal dönüşüm / perspektif de aynı bekleyen önizlemenin sayısal ayarlarını açar. Bekleyen dönüşüm yokken bağımsız gelişmiş sayısal pencereyi açar.

Kaynak ölçüsü ile görünüm ölçüsü ayrı gösterilir. Görünüm ölçüsü, döndürülmüş veya perspektifli katmanı içine alan eksene hizalı sınırdır. Kenar yüzdeleri üst ve sol kenarın uzunluğunu kaynak genişliği/yüksekliğiyle karşılaştırır; açı üst kenarın yönüdür. Bu değerler perspektifte tek bir genel ölçek/açı varmış gibi yorumlanmamalıdır.

## Diğer işlemlerle birlikte

Odak değişmesi ve sıradan arayüz yenilemeleri dönüşümü kapatmaz. Sekmeler arasında geçişte her belgenin önizlemesi kendi tuvalinde kalır. Sığdır/gerçek boyut/zoom komutları ve yeniden Ctrl+T de önizlemeyi korur. Araç değiştirme, katman değiştirme, özellik düzenleme, kayıt, dışa aktarma ve kapatma işlemleri önce dönüşümü uygular. Kayıt/dışa aktarma/kapatma penceresini iptal etmek uygulanan dönüşümü silmez; geri alabilirsiniz. Görüntü katmanı ekleme ise dosya seçimi ve görüntü doğrulaması bitene kadar önizlemeyi değiştirmez; dosya seçiciyi iptal etmek dönüşümü uygulamaz. Önizleme açıkken Geri al / Yinele, bekleyen önizlemeyi iptal eder; belge geçmişine dokunmaz.

Başka bir gerçek belge işlemi önizlemenin kaynak durumunu değiştirirse eski önizleme güvenli biçimde kapatılır. Kilitli/gizli katman dönüştürülmez. Güvenli piksel, toplam bellek veya koordinat sınırını aşan sürüklemelerde son geçerli önizleme korunur; uyarı gösterilir.

## Kapsam

Dönüşüm varsayılan olarak bütün etkin katmana uygulanır. Hedef katmanın adı ve kapsam tuvalde gösterilir. Piksel seçimi varsa seçim yerinde kalır; tüm-katman işlemi yalnız seçilmiş nesneye uygulanıyormuş gibi sunulmaz.

Seçim varken **Seçimi kopyala ve dönüştür** yolu, seçili görünen pikselleri ayrı bir katman adayı olarak önizler. Özgün katman olduğu yerde kalır. Bu yol kesme değildir; özgündeki nesneyi silmez veya taşımaz. Kopya raster piksellerden oluşur; özgündeki düzenlenebilir metin, maske ve ayarlar özgünde korunur. Esc kopyayı ve dönüşümü birlikte iptal eder; Uygula kopya ile dönüşümü tek geri alma adımında kaydeder. Belgedeki seçim ve özgün katmanın pikselleri değişmez. Kırpılmış katmandan kopyalanan piksellerde, kırpma temelinin görünür alfa kapsamı kopyaya işlenir; kopya bağımsızdır ve taşınınca önceden gizli pikselleri açığa çıkarmaz.

Özgün katmanın yalnız seçilmiş piksellerini yerinde dönüştürme, bağımsız maske dönüşümü ve warp bu kapsamda değildir. Sayısal pencerenin perspektif/eğme/aynalama seçenekleri korunur; tutamaçlar bunlara ayrı serbest köşe veya warp hareketleri eklemez.

## Büyütmeden boyamaya geçiş

Kaynak koruyan dönüşümden sonra piksel boyamak için görünümün rasterleştirilmesi gerekir. Boyama araçlarında kalıcı açıklama, **Rasterleştir…** ve **Yeni boş katmana boya** yollarını gösterir. Rasterleştirme onayında metin, maske, dönüşüm ve katman ayarlarına etkisi açıklanır; otomatik uygulanmaz. İptal belgeyi değiştirmez. Yeni boş katman yolu özgünün düzenlenebilirliğini korur. Yeni boya katmanı ve içe aktarılan görüntü katmanı, etkin katmanın kırpma grubunun üstüne yerleşir; var olan kırpılmış katmanların temelini değiştirmez. Rasterleştirme ile ardından yapılan boyama ayrı geri alma adımlarıdır.

Davranış kapsamı `tests/test_usability.cpp`, `tests/test_positioning.cpp` ve `tests/test_transforms.cpp` içindeki `onCanvas…` testlerinde gerçek QWidget fare/klavye olayları, kaynak piksel/maske/metin karşılaştırmaları ve dosya round-trip ile denetlenir. Bu, fiziksel tablet veya bütün işletim sistemi sürümleri için doğrulama değildir.
