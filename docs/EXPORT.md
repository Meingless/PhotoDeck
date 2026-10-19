# Uygulama içi görüntü dışa aktarımı

`0.10.0-ea.4` erken erişim adayında üstteki **Dışa aktar** düğmesi veya
**Ctrl+Shift+E**, PhotoDeck'in kendi çıktı ekranını açar. Konum, dosya adı, biçim,
biçime uygun seçenekler ve son **Dışa aktar** düğmesi aynı ekrandadır.

- **Konum / dosya** alanında tam dosya yolunu düzenleyin. **Gözat…**, sistemin
  klasör seçicisini açar ve seçilen klasörde mevcut dosya adını korur.
- Oran kilidi açıkken eş boyut 1 veya 16384 sınırına gelirse düzenlenen boyut da geri ayarlanır; kilitli oran sessizce bozulmaz. Piksel yuvarlaması tamsayı boyutlarla sınırlıdır.
- **Biçim**, gerçek dosya kodlamasını belirler. PNG, JPEG, WebP, BMP ve TIFF
  arasından bu sistemde kurulu Qt kodlayıcılarının destekledikleri gösterilir.
  Yeni bir kodlayıcı eklenmez.
- Seçilen biçim, dosya uzantısını da düzenler. `.jpeg` ve `.tif` eşdeğerleri
  korunur; bilinen başka raster uzantısı değiştirilir. Bilinmeyen uzantıya
  seçilen uzantı eklenir. Örneğin `proje.pdeck`, PNG seçiliyken `proje.pdeck.png`
  olur; katmanlı proje dosyasının üzerine görüntü yazılmaz.
- JPEG ve WebP için kalite seçilebilir. PNG/WebP saydamlığı korur; JPEG saydam
  alanları beyaz arka plana birleştirir. BMP için ICC kapatılır. Biçimle ilgili
  açıklama doğrudan ekranda görünür.
- Genişlik, yükseklik, oran kilidi, DPI ve mevcut RGB/ICC seçenekleri aynı
  ekrandadır. Çıktı boyutu 16 MP sınırına tabidir. ICC kapalıysa sRGB çalışma
  pikselleri dışa aktarılır; bu ekran CMYK veya soft-proofing eklemez.
- Var olan bir dosya ancak açık **Üzerine yaz** onayından sonra değiştirilir.
  Onayı iptal etmek ayarlara geri döner. Ana **İptal**, hiçbir çıktı yazmaz.
  Yazma, mevcut atomik `QSaveFile` yolunu kullanır.

Katmanlar, akıllı nesnelerin gömülü kaynakları, proje yolu, kaydedilmemiş durum
ve geri alma geçmişi dışa aktarımla değiştirilmez. Açık katman dönüşümü varsa
çıkış, o anki önizlemeyi kullanır ve dönüşümü belgeye uygulamaz. İptal de bu
önizlemeyi korur. Pencere açıkken kaynak belge/katman içeriği veya dönüşüm
önizlemesi değişirse çıktı yazılmaz; ekranı yeniden açmanız istenir.

Dosya menüsündeki eski **Boyut / DPI / ICC ile raster çıktı…** komutu aynı
birleşik ekranı açar. PDF ve yazdırma yolları ayrı kalır.
