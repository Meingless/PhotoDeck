# PhotoDeck arayüz varyasyonları

10 ayrı tasarım yönü. Bunlar arayüzü seçmek için hazırlanmış etkileşimli önizlemelerdir; çalışan Qt uygulamasına henüz uygulanmadılar. Sahne, mevcut Horizon örneğinin renk ve geometrileriyle çizilir.

| Tasarım | Yerleşim | Uygun kullanım |
|---|---|---|
| Grafit | Solda iki sütunlu araçlar, sağda renk ve katmanlar | Klasik masaüstü düzenleme |
| Kağıt | Açık sıcak yüzeyler, adı yazılı dikey araçlar | Sade ve kolay öğrenilen düzen |
| Odak | Yüzen alt araç çubuğu, küçük katman paneli | Tuvale odaklanan çalışma |
| Fotoğraf Masası | Alt fotoğraf şeridi, sağda ton ayarları | Birden fazla fotoğrafla düzenleme |
| Çalışma Tezgahı | Solda özellikler, sağda araçlar | Teknik çalışma düzeni |
| Şerit | Üstte gruplandırılmış, isimli komutlar | Araçların adını görmek isteyenler |
| Çizim Atölyesi | Fırça uçları, renk paleti ve katmanlar | Çizim ve boyama |
| Kompakt | Yatay araç rayı, sıkı boşluklar | Küçük dizüstü ekranları |
| Dokunmatik | Büyük düğmeler, altta isimli araçlar | Dokunma ve kalemle kullanım fikri |
| Modüler | Ayrı araç ve özellik panel blokları | Esnek çalışma alanı fikri |

`photodeck-variants.html` sohbet içinde gösterilen kaynak parçadır. `preview.html` kontrol için oluşturulan bağımsız önizlemedir. Tarayıcıda açarak da tasarımlar arasında geçilebilir. Görüntüler `previews/01-graphite.png` ile `previews/10-modular.png` arasındadır; `mobile-*` dosyaları dar ekran kontrolleridir. `overview.png` toplu görünümdür.

Araç seçimi, katman seçimi/görünürlüğü, renk kaydırıcıları, yakınlık, fırça ucu ve fotoğraf şeridi etkileşimleri önizleme durumunu değiştirir. Kaydet/dışa aktar komutları proje dosyası yazmaz. Modüler panelin taşıma işareti görsel bir öneridir; sürükleme uygulanmamıştır.

Doğrulama: 10 tasarım 1024 px ve 320 px genişlikte tarayıcıda işlendi. JavaScript hatası, yatay taşma veya panelin uygulama sınırını aşması bulunmadı. Katman görünürlüğü, görünen fırça düğmeleri ve renk ayarları kontrol edildi. Önceki/sonraki gezinme 10 tasarımın tamamını açabiliyor. Sonuçlar `verification.json` ve `carousel-verification.json` dosyalarında.

Tercih: Grafit alışılmış bir başlangıç; Kompakt küçük ekranlar için güçlü alternatif. Tasarım önizlemelerinin RAM/FPS ölçümü yapılmadı; düşük sistem hedefi üretim uygulamasında ölçülmelidir.
