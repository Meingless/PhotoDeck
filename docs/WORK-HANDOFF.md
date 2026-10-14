# Çalışma durumu — 14 Ekim 2026

## Güncel durum: 0.8 yayımlandı

Fırça geometrisi ve tablet girdisi aşaması [0.8.0-ea.1](https://github.com/Meingless/PhotoDeck/releases/tag/v0.8.0-ea.1) olarak yayımlandı. Kaynak commit `e6767ab65ea8b1e83da7ef24f1220340256987be`; [CI](https://github.com/Meingless/PhotoDeck/actions/runs/38601577984) dört platformda başarılı. Her hedefte 327 başarılı kontrol (başlangıç/bitiş dahil), on test grubu; Linux/Windows paket testleri ayrıca başarılı. Yerel ASan/UBSan ve qmake fırça grubu başarılı; leak detection kapalı.

Ctrl+Alt+B ile uç açısı/yuvarlaklığı, sürekli darbe aralığı ve basınç/eğim ayarları vardır. Kalemin silgi ucu piksel ve maskede çalışır. Boyama dışı araçlarda Qt'nin mevcut fare benzetimi korunur. Fiziksel tablet testi yapılmadı; Qt aygıt/olay fixture'ları kullanıldı. Fırça ayarları sekme oturumuna aittir; .pdeck v5 değişmedi.

Yerel `bin/photodeck` 0.8'dir; hazır paketler `dist/applications/0.8.0-ea.1/` içindedir. Önceki paketler ve yerel 0.6/0.7 başlatıcı kopyaları korunur. Sekiz yüklenen asset'in boyutu ve GitHub SHA-256 değeri yerel dosyalarla eşleşti. Native ve FUSE-free AppImage açılışı başarılı; 206 portable payload dosyası aynı. Kanıtlar `artifacts/brush-release/`, `artifacts/brush-ui/`, `artifacts/brush-results.txt`, `brush-sanitizer-results.txt`, `brush-qmake-results.txt` ve teslimdeki `DOGRULAMA.md` içindedir.

0.7'nin RGB ICC/PDF/yazdırma kapsamı korunur. Genel Photoshop karşılaştırması tamamlanmış değildir; hedef aktiftir. Özel görüntü/ABR uçları, kalıcı fırça kütüphanesi, scatter/jitter/texture, smoothing/simetri ve diğer kalan gereksinimler `docs/PHOTOSHOP-COMPARISON.md` içinde açık kalır.

## Tarihsel durdurma kaydı

Aşağıdaki 0.6 notları önceki durdurma anına aittir. Test lambdası derleme hatası, native PDF sayfa yönü, milimetre margin ve tam sayfa yazıcı margin sorunları 0.7'de düzeltildi. Çıktı değişiklikleri artık commit edilip yayımlanmıştır. Log dosyaları sonraki doğrulamalarla yenilenmiştir.

## Kararlı sürüm

- Son commit: `9bba7a8f4860105a83da1ba20279613ed36fab23`.
- Yayımlanan erken erişim sürümü: https://github.com/Meingless/PhotoDeck/releases/tag/v0.6.0-ea.1
- Kararlı yerel başlatıcı: `bin/photodeck`; paketler: `dist/applications/0.6.0-ea.1/`.
- Önceki teslim kaydındaki başarılı CI: https://github.com/Meingless/PhotoDeck/actions/runs/38459630259

## Korunan, tamamlanmamış yerel değişiklikler

PDF dışa aktarma, yazdırma, boyut/DPI/kalite seçenekleri ve RGB ICC profilleri üzerinde çalışıldı. Değişiklikler çalışma ağacında korunuyor; commit edilmedi ve GitHub'a gönderilmedi. Sürüm numarası artırılmadı.

Yeni modüller: `src/output.*`, `src/outputdialog.*`; yeni testler: `tests/test_output.cpp`, `tests/output.pro`. Document/MainWindow, CMake/qmake ve Linux paketleme dosyalarında bağlantı değişiklikleri var.

## Son doğrulama durumu

Son derleme başarısız oldu. `tests/test_output.cpp:234` içinde bool döndüren lambda'daki `QCOMPARE` makrosu void dönüş üretiyor. Kanıt: `artifacts/output-build.log`. Derleme durduğu için bu çalışmanın CTest aşaması başlamadı; `artifacts/output-results.txt` oluşmadı. Yeni çıktı özellikleri doğrulanmış kabul edilmemeli.

Derleme oturumu 18036 artık mevcut değil; kontrol sırasında devam eden CMake/CTest işlemi bulunmadı. Yeni derleme başlatılmadı.

## Devam edildiğinde

Önce `git diff` ve yeni çıktı modüllerini incele; test lambdasının dönüş sorununu düzeltip derleme ve ilgili testleri çalıştır. Yerel geliştirme derlemesini yayımlanmış 0.6 paketleriyle karıştırma. PDF tek sayfalı düzleştirilmiş raster çıktıdır; fiziksel yazıcı testi yapılmadı. Photoshop karşılaştırmasının kalan kapsamı `docs/PHOTOSHOP-COMPARISON.md` içinde; genel hedef tamamlanmış değildir.
