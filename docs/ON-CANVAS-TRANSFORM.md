# Tuval üstü dönüşüm

Ctrl+T veya Katman → Kaynak koruyan serbest dönüşüm, etkin katmanın etrafında mint renkli bir dönüşüm kutusu açar. Kaynak piksel, düzenlenebilir metin ve maske birlikte korunur. Önizleme sırasında gerçek belge ve geri alma geçmişi değişmez.

- Kutunun içini sürükleyerek taşıyın. Shift hareketi yatay/dikey eksene sabitler.
- Köşeleri sürüklemek oranı korur; Shift serbest yatay/dikey ölçeklemeye geçer.
- Kenar ortasındaki tutamaç tek ekseni ölçekler; Shift iki eksenin oranını korur.
- Alt ile merkezden ölçekleyin. Bir tutamaç karşı kenarı geçtiğinde katman ters dönmez; küçük pozitif boyutta durur.
- Üstteki yuvarlak tutamaç katman merkezinde döndürür. Shift açıyı 15° adımlara sabitler.
- Ok tuşları 1 belge pikseli, Shift+ok 10 piksel taşır.
- Tekerlek yakınlaştırır; orta fare tuşu tuvali kaydırır. Tutamaçların ekran boyutu yakınlaştırmayla değişmez.
- Enter veya Uygula bütün sürüklemeleri tek geri alma adımında kabul eder. Esc veya İptal önizlemeyi hiçbir belge değişikliği olmadan kapatır. Kutunun içinde çift tıklamak da uygular.

Sayısal düğmesi, aynı önizleme üzerinde ölçek/açı/eğme/aynalama/perspektif ayarlarını açar. Pencereyi iptal etmek önceki tuval önizlemesine döner; kabul etmek tuval önizlemesini günceller. Sonucu tuvalde Uygula ile kaydedin. Ctrl+Alt+T veya Katman → Sayısal dönüşüm / perspektif de aynı bekleyen önizlemenin sayısal ayarlarını açar. Bekleyen dönüşüm yokken bağımsız gelişmiş sayısal pencereyi açar.

## Diğer işlemlerle birlikte

Odak değişmesi ve sıradan arayüz yenilemeleri dönüşümü kapatmaz. Sekmeler arasında geçişte her belgenin önizlemesi kendi tuvalinde kalır. Sığdır/gerçek boyut/zoom komutları ve yeniden Ctrl+T de önizlemeyi korur. Araç değiştirme, katman değiştirme, özellik düzenleme, kayıt ve kapatma işlemleri önce dönüşümü uygular. Kayıt/kapatma penceresini iptal etmek uygulanan dönüşümü silmez; geri alabilirsiniz. Önizleme açıkken Geri al / Yinele, bekleyen önizlemeyi iptal eder; belge geçmişine dokunmaz.

Başka bir gerçek belge işlemi önizlemenin kaynak durumunu değiştirirse eski önizleme güvenli biçimde kapatılır. Kilitli/gizli katman dönüştürülmez. Güvenli piksel, toplam bellek veya koordinat sınırını aşan sürüklemelerde son geçerli önizleme korunur; uyarı gösterilir.

## Kapsam

Dönüşüm bütün etkin katmana uygulanır. Seçilmiş pikselleri ayrı dönüştürme, bağımsız maske dönüşümü ve warp yoktur. Sayısal pencerenin perspektif/eğme/aynalama seçenekleri korunur; tutamaçlar bunlara ayrı serbest köşe veya warp hareketleri eklemez. Kaynak koruyan dönüşümden sonra piksel boyamak için Katman → Görünümü rasterleştir gerekir.

Davranış kapsamı `tests/test_transforms.cpp` içindeki `onCanvas…` testlerinde gerçek QWidget fare/klavye olayları, kaynak piksel/maske/metin karşılaştırmaları ve dosya round-trip ile denetlenir. Bu, fiziksel tablet veya bütün işletim sistemi sürümleri için doğrulama değildir.
