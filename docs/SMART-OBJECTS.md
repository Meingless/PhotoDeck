# Gömülü akıllı nesneler: ilk erken erişim aşaması

Bu belge `0.10.0-ea.3` erken erişim adayının kaynak kapsamını anlatır. Temel, yayımlanmış
`0.10.0-ea.2` kaynağıdır. Bu özellik EA2 paketlerinde yoktur.

## Kullanım

1. Görünür, kilidi açık bir piksel katmanı seçin. Katman → Akıllı nesne alt
   menüsünden veya katmanın sağ tık menüsünden **Akıllı nesneye dönüştür** kullanın.
2. Katman satırındaki **Akıllı nesne** bilgisi korunan kaynağı belirtir. Normal
   katman dönüşümü, konum, opaklık, karışım, kırpma, maske ve mevcut renk/filtre
   ayarları kullanılabilir.
3. **İçeriği değiştir…** ile başka bir görüntü seçin. Yeni görüntünün çözümlenmiş
   tam çözünürlüklü pikselleri projeye gömülür. Dış dosyayla canlı bağlantı kurulmaz.
4. Yeni görüntü, nesnenin dönüşüm öncesi sabit çerçevesini doldurur. Farklı en-boy
   oranı bu çerçeveye esnetilir. Katmanın tuvaldeki dönüşümü ve konumu, maskesi,
   efekt ayarları ve kırpma ilişkisi değişmez. Kaynak görüntü bu yerleştirme için
   kalıcı olarak küçültülmez; tam pikselleri korunur.
5. Dönüştürme, içerik değiştirme ve açıkça onaylanan rasterleştirme geri alınır.
   İçeriği değiştir penceresini iptal etmek belgeyi veya açık dönüşüm önizlemesini
   değiştirmez. Başarılı değiştirme öncesinde açık katman dönüşümü tamamlanır.

## Kaynak ve çerçeve

EA2 zaten geometrik dönüşümlerde kaynak pikselleri ve mevcut filtre değerlerini
koruyordu. Yeni özellik bunun adını değiştirmekle sınırlı değildir: ayrı nesne
kimliği, doğrudan kaynak boyamaya karşı koruma, değiştirilebilir gömülü içerik,
sabit yerleştirme çerçevesi ve bunları saklayan proje biçimi ekler.

Her akıllı nesnenin kendi kimliği vardır. Katmanı çoğaltmak aynı başlangıç
piksellerini paylaşan bağımsız bir nesne oluşturur; bir kopyanın içeriğini
değiştirmek diğerini değiştirmez. Geçmişin eski kaynakları tutması mevcut bellek
bütçesiyle sınırlıdır. Değiştirilmiş eski kaynaklar sonsuza kadar arşivlenmez.

Seçim yokken **Ctrl+J**, **Katmanın tamamını çoğalt** gibi bütün katmanı
çoğaltır; akıllı nesnenin gömülü kaynağı, sabit çerçevesi, dönüşümü, maskesi ve
efektleri korunur, kopya bağımsız nesne kimliği alır. Etkin bir seçim varsa
**Ctrl+J** seçili görünür piksellerden sıradan bir piksel katmanı üretir.

Maske çerçeve koordinatlarındadır. Yeni içeriğin piksel boyutu değişse de maske
yeniden örneklenmez. Dönüşümsüz nesnenin maskesi boyanabilir; dönüştürülmüş
maskeyi tuval üzerinde boyama, önceki sürümde olduğu gibi desteklenmez.

Maske ve efekt yokken farklı çözünürlüklü yeni kaynak ile katman dönüşümü tek
çizim adımında birleştirilir; büyütme, gömülü yüksek çözünürlüklü ayrıntıyı
kullanır. Bu ilk aşamada etkin maske veya efekt varsa işleme sabit çerçeve
çözünürlüğünde yapılır. Böyle bir nesnenin büyük büyütmede yeni kaynağın tüm
ayrıntısını göstermesi garanti edilmez; gömülü tam çözünürlüklü pikseller yine
korunur. Yüksek çözünürlüklü maske/efekt işleme ayrı bir sonraki adımdır.

## Açık rasterleştirme

Akıllı kaynağa fırça, silgi, kova, klonlama veya Delete ile doğrudan yazılmaz.
Kaynağı koruyarak üstüne boyamak için **Yeni boş katmana boya** kullanılabilir.
**Görünümü rasterleştir…** onaylandığında görünen sonuç sıradan piksel katmanına
çevrilir: gömülü kaynak kimliği, ayrı dönüşüm/maske/efekt düzenlenebilirliği
kalkar. Geri al bunları geri getirir.

Bu ilk aşamada akıllı nesne içeren bütün-görüntü boyutlandırma, 90° döndürme,
aynalama ve akıllı kaynağı pişirecek birleştirme/düzleştirme işlemleri durdurulur.
Önce ilgili nesneyi açıkça rasterleştirin veya kaynak koruyan katman dönüşümünü
kullanın. Tuval boyutu ve tuval kırpma kaynağı korur. Seçimi kopyala ve dönüştür
ayrı bir sıradan piksel katmanı üretir; nesneden seçim kesmek engellenir.

Metin katmanı doğrudan akıllı nesneye çevrilmez. Metin düzenlenebilirliğini
bırakmak istiyorsanız önce açıkça rasterleştirin.

## Proje uyumluluğu ve kurtarma

- Akıllı nesne içeren normal proje ve kurtarma kopyası **v7** kullanır.
- EA2 ve daha eski okuyucular v7 dosyasını açmayı reddeder; gömülü içeriği
  sessizce sıradan katman olarak açıp kaybetmez.
- İlk v7 kaydında eski okuyucu uyumsuzluğu açıklanır. Eski proje üzerine biçim
  yükseltme mevcut güvenli kopya/uyarı yolunu kullanır; iptal özgünü korur.
- v1–v6 projeleri açılabilir. Akıllı nesne içermeyen normal kayıt, adlandırılmış
  seçim varsa v6, yoksa v5 olarak kalır. Son akıllı nesneyi rasterleştirmek veya
  silmek bu düşük sürümlere dönüşü mümkün kılar.
- v7, nesne kimliği ve sabit çerçeve boyutunu kaynak pikselleriyle birlikte
  saklar. Dosya yolu, dış bağlantı veya ayrı gizli yedek dosya oluşturulmaz.

## Bellek ve sınırlar

Tek kaynak görüntüsü saklanır; dönüştürme sırasında ikinci bir tam orijinal
piksel tamponu üretilmez. Qt'nin yazılana kadar paylaşımı, mevcut tek katman çizim
önbelleği ve önbelleksiz geçmiş/kurtarma anlık görüntüleri kullanılır. Kaynak ve
çerçeve 16 MP, belge katmanları 256 MiB, geçmiş 128 MiB sınırlarına tabidir.
Bu sınırlar toplam süreç RAM'i garantisi değildir; geçici çizim/kodlama
tamponları ve Qt ek maliyeti vardır. Belirli bir cihazda 100 MB sözü verilmez.

Bu aşama bağlantılı dosyalar, iç içe belgeler, ayrı içerik-düzenleme sekmesi,
vektör/RAW/PSD gömme veya yeniden sıralanabilir Photoshop akıllı filtre yığını
eklemez. Mevcut katman renk/ton/filtre değerleri korunur ve düzenlenebilir.
Son EA3 kaynağının dört platform CI/paket kanıtı yayım sırasında ayrıca üretilir;
önceki geliştirme sonucu yeni metadata kaynağının doğrulaması değildir.
[Yayın kapıları ve sürüm notları](releases/0.10.0-ea.3.md).
