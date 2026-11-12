# PhotoDeck yeni arayüz yönleri

Güncelleme: Bu ilk taslak turundan sonra kullanıcı Photoshop referanslı **Klasik Pro** tasarımını ve özgün PhotoDeck logosunu seçti; [uygulama durumu](IMPROVEMENTS-2026-11-08.md). Aşağıdaki metin ilk fikir aşamasının kaydıdır.

Kullanıcı 8 Kasım 2026'da önceki öneri listesinin uygulanmasını istedi; önceliği uygulamanın arayüzünü tamamen yenilemek ve önce farklı taslakları görmek olarak belirledi. Logo ve bütün yerleşimler değişebilir. Bu aşama yön seçimi içindir; bir tasarım henüz seçilmedi ve üretim arayüzüne uygulanmadı.

## Karşılaştırılacak yönler

| Yön | Yerleşim fikri | Marka konumu | Ana kullanım |
| --- | --- | --- | --- |
| Pusula | Geniş tuval, yüzen araç dock'u ve küçük bağlam paneli | Alt köşede sade işaret | Tuvalde doğrudan çalışma |
| Folyo | Solda belge/nesne ağacı, altta geniş özellik şeridi | Sol çalışma alanı başlığında | Katmanlı kompozisyon ve metin |
| Akış | Üstte Hazırla / Düzenle / Bitir; sağda tek görev paneli | Aşama navigasyonuyla birlikte | Araçları adım adım keşfetme |
| Kontak | Yatay komut grupları, editoryal özellik paneli ve belge şeridi | Üst solda tipografik marka | Fotoğraf seçimi ve tonlama |
| Sahne | Serbest tuval, bağlamsal araçlar, köşelerde gezgin ve katman rafı | Üst ortada | Geniş ve sakin çalışma yüzeyi |

Aynı Horizon içeriği kullanılır. Taslak kontrolleri yalnız önizleme durumunu değiştirir; proje kaydı veya gerçek görüntü düzenleme işlemi yapmaz. Görünür taslaklar sohbetin görsel karşılaştırmasındadır.

Kaynak: `/home/omerkerem/.codex/visualizations/2026/11/08/01a1bc76-df6d-7011-9a94-2c5d9e224eab/photodeck-new-directions.html`.

Doğrulama: beş tasarım 1024/736/320 px genişlikte açık ve koyu temada tarayıcıda kontrol edildi (30 görünüm). Yatay/dikey taşma, gizli temel katman/opaklık kontrolü veya JavaScript hatası bulunmadı. Katman seçimi, bağımsız opaklık, göster/gizle, karşılaştırma ve mevcut sıfırlama kontrolleri doğrulandı. Taslaklar ayrıca görsel olarak incelendi; bu kontroller Rust uygulamasındaki gerçek özelliklerin uygulandığı anlamına gelmez.

## Seçimden sonraki sıra

1. Seçilen yön veya birden çok taslağın birleşimi üzerinden logo, navigasyon ve panel hiyerarşisini netleştirme.
2. Rust arayüzüne uygulama; mevcut modal koruması, sabit belge kimlikleri, kaydet/kapat ve undo değişmezlerini koruma.
3. [Önceki önerileri](IMPROVEMENTS-2026-11-08.md) yeni arayüz içinde uygulama. Kullanıcı bu yeniliklerin yapılmasına izin verdi; tasarım seçimi bu sıranın ilk bağımlılığıdır.
4. Gerçek pencere boyutları, klavye akışı ve platform olanaklarını doğrulama. Tablet/yazıcı gibi donanım gerektiren maddelerde uygulama ile fiziksel doğrulamayı ayrı raporlama.

Bu belge bir tasarım seçimi veya bütün önerilerin tamamlandığı iddiası değildir.
