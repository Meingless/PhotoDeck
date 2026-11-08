# Seçim geçmişi ve kayıtlı seçimler

## Düzenleme akışı

- Dikdörtgen, elips, kement, çokgen kement ve sihirli değnek hareketleri tamamlandığında tek bir geçmiş adımı oluşturur. Aynı kapsamı yeniden seçmek geçmişi doldurmaz; iptal edilen hareket hiçbir adım eklemez.
- Seçimi kaldır (Ctrl+D), Yeniden seç (Ctrl+Shift+D), tümünü seç, ters çevir, yumuşat, büyüt/küçült ve katman saydamlığından seçim de geri alınabilir.
- Seçim ve yeniden seçim kaynağı, piksel/katman düzenlemeleriyle birlikte geri alınır ve yinelenir. İptal edilen bir boyama hareketi mevcut seçimi kaldırmaz.
- Etkin seçimde sıfır kapsam bütün pikselleri korur. Seçimin kaldırılması ise düzenlemeyi kısıtlamaz. Bu iki durum geçmişte, dönüşümlerde ve dosyalarda ayrı kalır.
- Yalnızca etkin seçimin değişmesi belgeyi kirli yapmaz. Etkin seçim ve yeniden seçim kaynağı geçici çalışma bağlamıdır; normal proje kaydına dahil değildir.

Tüm seçim komutları üst düzey **Seçim** menüsündedir (Alt+S): tümünü seç/kaldır/yeniden seç, adlandırılmış seçimler, ters çevir/yumuşat/büyüt/küçült ve katman alfasını seç. Piksel silme, kopyala/yapıştır ve fırça kütüphanesi **Düzenle** menüsünde kalır.

## Adlandırılmış seçimler

Seçim → Seçimi kaydet, o andaki tam 8 bit kapsamı veya sert dikdörtgeni bir adla saklar. Aynı adlı seçimin değiştirilmesi arayüzde onay ister. Kayıtlı seçimi yükle, yeni seçim/ekle/çıkar/kesiştir kiplerini sunar; Kayıtlı seçimi sil geri alınabilir.

Adlandırılmış seçim eklemek, değiştirmek ve silmek kaydedilecek belge içeriğini değiştirir. Bu işlemler geri alınırken gerçek kayıt noktası da korunur. Yumuşak kenarlar ve etkin-boş seçimler kayıpsız saklanır.

Kırpma, görüntü boyutu, 90° döndürme, yatay/dikey çevirme ve tuval boyutu işlemleri hem etkin seçimi hem yeniden seçim kaynağını hem kayıtlı seçimleri aynı belge koordinatlarına taşır. Kırpma dışına çıkan etkin seçim, kısıtlamasız hale gelmez; etkin-boş kalır.

## Biçim uyumluluğu

- Adlandırılmış seçim yoksa normal kayıtlar önceki v5 katman kodlamasını kullanır. Sadece seçim yapmak veya eski bir v5 projeyi açıp kaydetmek v6 yükseltmesine yol açmaz.
- Adlandırılmış seçim içeren normal projeler v6 gerektirir. Yayımlanan PhotoDeck 0.9.1 bu dosyaları açamaz. İlk v6 kaydında arayüz bunu açıklar. v5 özgün dosya üzerine yazılmak yerine farklı adlı kopyaya yönlendirilir; iptal orijinali ve çalışma geçmişini korur.
- Son kayıtlı seçim silindiğinde sonraki normal kayıt yeniden v5 olur. v1–v5 okuma desteği korunur; eski v1–v4 özgün dosyalar için önceki korumalı v5 kopya akışı sürer.
- Kurtarma kopyaları, etkin seçim veya yeniden seçim kaynağı varsa açık bir v6 bağlam bayrağıyla bunları da saklar. Bu geçici kopyalar özgün dosyayı, belge yolunu, kaynak biçim sürümünü veya kayıt noktasını değiştirmez. Yalnızca seçim değiştirilmiş temiz belgeler için ek kurtarma kaydı gerekmez; kirli belgelerde sonraki kurtarma seçimin son halini de içerir.

v6, değişmeyen v5 katman kayıtlarının sonuna bir bağlam bayrağı, kayıtlı seçim sayısı, ad/kapsam kayıtları ve isteğe bağlı kurtarma bağlamı ekler. Kapsam türü açıkça kısıtlamasız, dikdörtgen veya tam tuval boyutunda satır-major 8 bit maskedir. Genel görüntü çözücüsü kullanılmaz; genişlik, yük uzunluğu ve gerçek hizalanmış satır belleği ayrılmadan önce sınanır. Bilinmeyen türler, yanlış boyutlar, yinelenen/geçersiz adlar, eksik veya artan baytlar reddedilir. Hatalı yükleme mevcut belgeyi veya kullanılabilir geçmişini değiştirmez.

## Sınırlar ve doğrulama

En fazla 16 kayıtlı seçim, ad başına 128 UTF-16 kod birimi ve etkin seçim/yeniden seçim/kayıtlı maskelerin toplamında 64 MiB çözülmüş maske yükü tutulur. Paylaşılan maskeler geçmiş belleğinde yalnızca bir kez sayılır. Geçmiş, canlı belge verisine ek olarak en fazla 128 MiB ve 30 adımla sınırlıdır; türetilmiş sınır-çizgisi önbellekleri geçmiş kopyalarında tutulmaz.

`test_selection_workflow.cpp`, seçim/piksel/katman geçmişini, iptal/yinelenen hareketleri, yedi belge dönüşümünü, etkin-boş kapsamı, adlandırılmış seçimleri, v5 bayt uyumluluğunu, v6/kurtarma turunu, bozuk/kesilmiş kayıtları ve hizalı satır bellek sınırını kapsar. Arayüz testleri yeniden seçim, adlandırma/yükleme/silme, iptal, v6 uyarıları ve v5 orijinalini koruyan kopya kaydını yürütür.

`tests/fixtures/v5-demo.pdeck`, değiştirilmemiş `e209345d91690ba091e535b13f2f6b5a4cb7aa4f` kaynak temelinin Qt 6.8.3 Release uygulamasıyla `--write-demo` üzerinden üretildi. SHA-256: `b4812970233431b8450403398bdf09ddb3648a5fae6104dd5c46e12f0737a007`.
