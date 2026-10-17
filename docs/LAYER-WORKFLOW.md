# Katman satırını sürükleyerek sıralama

Katman listesindeki ad veya küçük resmi sürükleyip iki satır arasındaki vurgulu çizgiye bırakın. Listenin üstü kompozisyonun üst katmanıdır. Kenarda bekleyerek uzun listeyi kaydırabilirsiniz. Tek katman taşınır; aradaki katmanlar yer değiştirir. Üste/alta taşı düğmeleri ve sağ tık menüsü de kullanılabilir.

Katman içeriğinin sırasını gerçekten değiştiren her bırakma tek geri alma adımıdır. Aynı yere bırakmak veya sürüklemeyi Esc ile iptal etmek geçmişe adım eklemez, belgeyi değişmiş göstermez ve yineleme dalını silmez. Adı, pikselleri, maskesi ve bütün ayarları tamamen aynı olan satırlar arasında taşımak yalnız seçili satırı değiştirebilir; mevcut üste/alta taşı davranışındaki gibi belgeyi kirletmez ve yinelemeyi silmez. Önceden açık ad düzenlemesi veya tuval dönüşümü önce kendi adımıyla tamamlanır.

Bu, yerel işletim sistemi sürükle-bırak oturumu başlatmayan bir liste içi fare hareketidir; yalnız aynı pencerenin aynı açık belgesinde geçerlidir. Belge/sekme/katman değişirse, bir düzenleme başlarsa veya kaynak silinirse eski sürükleme uygulanmaz. Liste dışına bırakmak, Esc, odak kaybı veya fare yakalamasının kesilmesi işlemi iptal eder. Başka pencereden ve dış uygulamadan gelen bırakmalar katman sıralamasına dönüşmez. Görünürlük kutusu kendi işlemini korur; kutudan veya satır içi ad düzenleyicisinden katman sürüklemesi başlamaz. F2, görünürlük kutuları, ok tuşları, sağ tık ve metne çift tıklama çalışmaya devam eder.

Etkin katmanın kimliği, kaynak pikseller, maske, metin, geometri, renk ayarları ve seçimler korunur. Kilit, mevcut üste/alta taşı komutlarındaki gibi sıralamayı engellemez. Geri alma, mevcut uygulama davranışına uygun olarak maske düzenleme modunu kapatır.

Kırpma ilişkileri sıraya bağlıdır: tek bir tabanı veya kırpılmış çocuğu taşımak görünümü ve bağlandığı tabanı değiştirebilir. En alta gelen katmanın kırpması kaldırılır; diğer kırpma bayrakları korunur. Bir grup veya kırpma zinciri birlikte taşınmaz. Bu işlem proje biçimine alan eklemez.

Doğrulama: `tests/test_layer_ops.cpp` model sınırları/no-op/yineleme/tek adımlı geri alma ile kaynak ve seçim korumasını sınar. `tests/test_layer_drag.cpp`, gerçek Qt fare eşiği/hareket/bırakma, iptal, dış giriş ve eski hedef reddi ile dar/geniş pencerede ekleme çizgisi ve kaydırmayı offscreen ortamında sınar. Fiziksel fare/dokunmatik aygıt ve Windows/macOS pencere sistemi doğrulaması bu yerel teste dahil değildir.

Dar Dokunmatik yerleşimde liste dış panel tarafından kısmen örtülüyorsa kenarda beklemek önce listeyi, liste sınırında da dış paneli kaydırır. Yalnız ekranda görünen bırakma noktaları kabul edilir; en ilk ve en son aralığa da ulaşılabilir. Etkin satıra kısa tıklamak açık tuval önizlemesini kapatmaz; sürükleme eşiği geçildiğinde önizleme tamamlanıp kaynak yeniden doğrulanır.
