# PhotoDeck 0.2.0 Early Access 1

AI içermeyen yerel fotoğraf ve katman düzenleyici. Erken erişim sürümüdür; önemli orijinalleri koruyun ve ayrı proje kopyalarıyla çalışın.

Varsayılan görünüm **3 · Odak**. Çalışma alanı menüsünden 10 preset arasında geçilebilir. Görünümü özelleştir veya Ctrl+Alt+P; yazı tipi/boyutu, simge boyutu, renkler, köşeler, panel tarafı/genişliği, araç adları, cetveller, menü/durum çubukları ve panel taşıma ayarlarını değiştirir. Dock panellerini başlıklarından taşıyıp ayırabilirsiniz. Odak'taki yüzen paneller sürüklenebilir; taşıma tutamacı odaktayken ok tuşları da çalışır. Kendi presetlerinizi kaydedebilir veya .pdui olarak taşıyabilirsiniz.

Görünüm ayarları bilgisayarın standart uygulama yapılandırma klasöründeki PhotoDeck/ui.ini dosyasında saklanır. .pdui yalnızca doğrulanan görünüm değerleri içerir; betik veya CSS çalıştırmaz. Belge pikselleri görünüm presetinden bağımsızdır.

## Paketler

- Linux x86_64: arşivi açın ve launch.sh çalıştırın. Qt kütüphaneleri pakette bulunur; sistem glibc, grafik sürücüsü ve fontlar sistemden kullanılır. CI derlemesi Ubuntu 22.04/glibc 2.35 tabanını hedefler.
- Windows x64: ZIP'i tamamen açın; PhotoDeck.exe çalıştırın. Qt DLL'leri ve görüntü eklentileri uygulamayla birlikte gelir. Pakette VC++ çalışma zamanı kurucusu varsa eksik çalışma zamanı için onu kullanın.
- macOS Intel / Apple Silicon: ZIP'i açıp PhotoDeck.app uygulamasını Applications klasörüne taşıyın. Erken erişim paketleri Apple Developer kimliğiyle imzalanmış veya noter onaylı değildir; macOS ilk açılışta güvenlik onayı isteyebilir. Ad hoc imza yayıncı kimliğini doğrulamaz.

PNG/JPEG/WebP/BMP ve .pdeck desteklenir. PSD/PSB, RAW, CMYK/ICC baskı, ileri vektör yolları ve tablet basıncı mevcut kapsamın dışındadır. 16 MP, 64 katman, 256 MiB piksel/maske ve 30 durum/128 MiB geçmiş sınırları vardır. Kurtarma kopyası 45 saniyede bir alınır; son değişiklikleri her zaman kapsamayabilir.

Derleme sonucu ile hedef donanım testi farklıdır: CI testleri ve paket açılış görüntüleri sağlanır; minimum donanım gereksinimi, bütün Linux dağıtımları, Windows sürümleri ve macOS cihazları test edilmiş değildir. SHA256SUMS.txt indirdiğiniz paketin bütünlüğünü kontrol eder.

Kaynak ve lisanslar paketle birlikte hazırlanır. Üçüncü taraf yazılım bildirimleri THIRD-PARTY.md ve licenses/ içindedir.
