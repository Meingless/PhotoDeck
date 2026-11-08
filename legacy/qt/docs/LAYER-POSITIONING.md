# Etkin katmanı hassas konumlandırma

Katman → **Etkin katman konumu…** veya katman özelliklerindeki **Konum…** düğmesi, etkin katmanın X/Y değerlerini piksel cinsinden ayarlar. İptal belgeyi değiştirmez; Uygula tek bir geri alma adımı oluşturur. Değerler aynıysa geçmişe adım eklenmez ve yineleme dalı korunur.

## Ok tuşlarıyla taşıma

Tuval klavye odağındayken **Taşı (V)** aracıyla ok tuşları etkin katmanı belge koordinatlarında 1 piksel, **Shift + ok** 10 piksel taşır. Yakınlaştırma ve ekran ölçeği bu adımı değiştirmez. Seçim olsa veya maske düzenleme açık olsa da tüm katman taşınır; seçim ve kaynak verileri korunur. Ctrl, Alt veya Meta ile ok birleşimleri bu komuta ait değildir. Metin alanları, menüler, katman ve geçmiş listeleri kendi ok tuşlarını kullanır. Ctrl+T dönüşüm önizlemesinin mevcut ok davranışı korunur.

Her ayrı basış bir geri alma adımıdır. Bir tuşu basılı tutunca oluşan otomatik tekrarlar tek adımda birleşir; yüzlerce tekrar geçmişteki diğer işlemleri tüketmez. Hareket hemen belgeye işlenir: tuş bırakılmadan kaydetme ve kurtarma da son görünen konumu kullanır. **Ctrl+Z** tüm basılı tutma hareketini geri alır. **Esc** mevcut seçim kaldırma davranışını korur; taşımayı geri almaz.

Tuşun gerçekten bırakılması, odağın veya aracın değişmesi, başka katmana/belgeye geçme ve başka bir belge işlemi bu birleştirmeyi bitirir. Kaydetme, undo/redo ve geçmişte gezinme sonrasında kalan otomatik tekrarlar yeni hedefi taşımaz; yeniden fiziksel basış gerekir. İşletim sisteminin tekrarlar arasında gönderdiği yapay tuş bırakmaları hareketi bölmez.

## Sayısal konum ve hizalama

X/Y, kaynak piksel dikdörtgeninin mevcut geometriyle dönüştürülüp dışa doğru tam piksele yuvarlanmış sınırının belge koordinatlarındaki sol üst köşesidir. Döndürülmüş ve perspektif katmanlarda da aynı tanım kullanılır. Kaynağın saydam kenarları, maskeyle gizlenen bölgeleri ve metin dolgu payları bu dikdörtgene dahildir. Alfa kırpması, görünen mürekkep sınırı veya kırpma zincirinin görünür alanı ölçülmez. Tamamen saydam ama boyutu geçerli bir katman da bu geometrik sınıra sahiptir.

**Hizala** düğmesi ve Katman → **Etkin katmanı hizala** menüsü hedefi açıkça seçtirir:

- **Tuval sınırlarına**: tüm tuvalin dikdörtgeni
- **Geçerli seçim sınırlarına**: mevcut seçimin sıfırdan büyük alfa kapsamını çevreleyen dikdörtgen. Seçim yoksa veya etkin seçim tümüyle sıfırsa bu hedef kullanılamaz; tuvale sessizce geçilmez.

Her hedef için sola, yatay ortaya, sağa, üste, dikey ortaya ve alta hizalama vardır. Yatay komutlar Y'yi, dikey komutlar X'i korur. Tam pikselde tam merkez mümkün değilse konum aşağı yuvarlanır: fazladan piksel sağda/altta kalır. Tuvalden büyük katmanlar da merkezlenebilir; negatif X/Y geçerlidir.

İşlemler yalnızca etkin katmanın belge ötelemesini (`offset`) değiştirir. Piksel, maske, metin düzenlenebilirliği, filtreler ve geometri matrisi korunur. Seçim ve kayıtlı seçimler taşınmaz. Çoklu katman seçimi, katmanlar arası hizalama, dağıtım ve otomatik yerleşim bu özelliğin kapsamında değildir.

Menü veya sayısal pencere açılırken bekleyen tuval hareketi/dönüşümü önce tamamlanır; konum ve hedef bundan sonra okunur. Bu önceki hareketin kendi geri alma adımı korunur. Konum penceresi açıkken belge/katman değişirse eski değerler yeni hedefe uygulanmaz.

Kilitli veya görünmez katmanlar konumlandırılmaz. Etkin katman yoksa, geometri geçersizse, düzenleme henüz tamamlanmamışsa veya istenen belge konumu/kaynak ötelemesi ±16777216 piksel sınırını aşarsa işlem belgeyi ve geçmişi değiştirmeden reddedilir. Pikseller tuval dışında da korunur.

Proje biçimine alan eklenmez. Kaynak ötelemesi mevcut `.pdeck` alanıyla saklanır; kayıtlı seçim içermeyen projeler v5, kayıtlı seçim içeren projeler v6 olarak kalır.

Doğrulama: `tests/test_positioning.cpp`, altı komutu iki açık hedefte kimlik/dönüş/ayna-eğme/perspektif geometrileriyle; tek adımlı undo/redo, no-op, sınır reddi, maskeli/metin kaynak koruması, v5/v6 tekrar açma ve gerçek pencere olaylarını sınar. Ok tuşu testleri belge piksel adımlarını, uzun otomatik tekrarları, ayrı basışları, kaynak/seçim korumasını, güvenli sınırları, odak/araç/katman/belge ve geçmiş/kayıt kesintilerini de kapsar. Focus/Paper/Touch çalışma alanları 1×/2× ekran ölçeklerinde görsel duman testinden geçirilir. Sayısal pencere ve kompakt düğmeler on hazır çalışma alanında 900×620 boyutta doğrulanır.
