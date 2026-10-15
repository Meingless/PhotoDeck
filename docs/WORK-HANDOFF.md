# Çalışma durumu — 15 Ekim 2026

## Güncel çalışma: rapor bulguları

`fix/report-modern-workspace` dalındaki çalışma rapordaki somut sorunlar, çalışma alanı arayüzü ve doğrulama boşluklarıyla sınırlıdır. Aşağıdaki 0.9 yayımlama kanıtları önceki kaynak sürümüne aittir; bu dalın derleme/test sonucu olarak kullanılmamalıdır. Bütün Photoshop karşılaştırmasını tamamlama, fiziksel tablet/yazıcı doğrulama veya imzalı yeni sürüm yayımlama bu çalışmanın sonucu değildir.

- Yeni `.github/workflows/validate.yml`, her PR ve `main` push'unda dört platformda CMake/CTest; Linux'ta ayrıca Clang ASan/UBSan çalıştıracak şekilde hazırlanır. Etiket/manuel paketleme akışı korunur.
- Yeni `tests/test_parser_safety.cpp`, küçük `.pdeck` ve `.pdbrush` girdilerinde tüm kesme noktalarını, sabit tohumlu byte mutasyonlarını ve reddetme sonrası hedefin korunmasını sınar. Kapsam güdümlü fuzzing sistemi değildir; testin eklenmesi parser açığı bulunduğu veya güvenliğin eksiksiz kanıtlandığı anlamına gelmez.
- README'deki 0.8'den kalan “görsel uç/fırça kütüphanesi açık hedef” ifadesi 0.9 kapsamıyla uyumlu hale getirilir. ABR ve diğer uygulanmamış özellikler açık kalır.
- Varsayılan Odak solda araçlar, ortada panellerin örtmediği tuval, sağda katmanlar/özellikler ve araca bağlı seçenek çubuğuyla düzenlenir. Ctrl+Alt+1…0 hazır çalışma alanlarını seçer; on preset ve kişiselleştirme korunur.
- Kayıtta dış değişiklik/silinme uyarısı iptal, ayrı kopya ve açık üzerine yazmayı sunar. Dosya kimliği/boyut/zaman/SHA-256 kontrolü iyimseldir; commit öncesi dar yarış aralığı tamamen kapanmaz. v1–v4 orijinalleri arayüzde farklı adla v5 kopyaya yönlendirilir.
- Kurtarma tamamlanan düzenlemelerden yaklaşık 5 saniye sonra, ayrıca 45 saniyelik periyodik kontrolde denenir. Canlı hareket bitene kadar ertelenir, sonra hemen yeniden denenir. Sıfır kayıp garantisi yoktur. Eşzamanlı PNG/dosya yazımının büyük belgelerdeki arayüz bekleme riski sürer.
- Son kaynak ağacında yerel Linux/Qt 6.8.3 Release CMake/CTest 14/14 geçti (12,24 sn): 379 Qt Test kontrolü (başlangıç/bitiş dahil), 351 test satırı; UI 15, kayıt 17, parser 5 başarılı kontrol. Temiz `a2aa2ed` temeli 11/11 geçti (7,30 sn). GCC 14.2 Debug ASan/UBSan son tekrarı 14/14 geçti (25,92 sn); Qt SDK instrument edilmedi, leak detection kapalıydı.
- qmake uygulama derlemesi yalıtılmış Qt/OpenGL sysroot ve açık OpenGL LIBS arama yoluyla geçti; `--version` 0.9.0-ea.1 ve başlangıç PNG kontrolü başarılı. On preset, Odak 900×620/1280×800, Fırça/Sihirli değnek/Kova seçenekleri ve kaydırılmış özellikler görsel kontrolden geçti; uyarı görülmedi.
- Üç eşleştirilmiş yerel 4096²/üç katman/192 MiB ölçümünde medyanların medyanı: değişmeyen UI yenileme 85,726→0,309 ms; sentetik taşıma+tuval+yardımcı yenileme 122,446→42,727 ms. Kompozisyon/filmşeridi hash'leri eşleşti, simge yeniden kullanımı 36/36. İkinci yol yaklaşık 43 ms'dir; 60 fps veya fiziksel cihaz garantisi değildir. Ayrı doğrulama teslimindeki rapor etiketi: `artifacts/perf/report.md`.
- [Taslak PR #1](https://github.com/Meingless/PhotoDeck/pull/1) açıldı. İlk [GitHub run 38736782571](https://github.com/Meingless/PhotoDeck/actions/runs/38736782571) dört hedefi derledi; Windows ve Clang sanitizer 14/14 geçti. Linux Release ile iki macOS hedefindeki tek başarısız grup UI diyalog otomasyonuydu: odaklı dosya adı alanı gerçek klavye girdisiyle değiştirilir; modeless About penceresi de beklenir. Test sürücüsüne anlık/kuyruklu modeless regresyonları eklendi. Güncel commit sonucu [PR kontrollerinden](https://github.com/Meingless/PhotoDeck/pull/1/checks), ayrıntılı matris [FEATURE-AUDIT.md](FEATURE-AUDIT.md) içinden okunmalıdır. Hiçbir yeni sürüm veya fiziksel cihaz sonucu iddia edilmez.

## Önceki yayımlama kaydı: 0.9

Modern arayüz, kalıcı fırça presetleri ve görsel uçlar [0.9.0-ea.1](https://github.com/Meingless/PhotoDeck/releases/tag/v0.9.0-ea.1) olarak yayımlandı. Kaynak commit `6e480ea7dd3308cc3e9c20c21b7f90fc7cca5dbb`; [CI](https://github.com/Meingless/PhotoDeck/actions/runs/38614460629) dört platformda başarılı. Hedef başına 342 başarılı kontrol (başlangıç/bitiş dahil), on bir grup; Linux/Windows paket testleri ayrıca başarılı. Yerel ASan/UBSan ve qmake kütüphane grubu başarılı; leak detection kapalı.

Menü, sekme, toolbar, panel/listeler, girişler, checkbox/slider/butonlar ve diyaloglar ortak modern tasarımla yenilendi. Varsayılan 3 · Odak, on preset ve .pdui v1 kişiselleştirmeleri korunur. On gerçek başlangıç PNG'si ile menü/kütüphane/ayar diyalogu görüntüleri kontrol edildi; `artifacts/modern-ui/` içindedir.

Düzenle → Fırça kütüphanesi / Ctrl+Alt+Shift+B tüm uç/tablet değerlerini ve aracı isimli presetlerle saklar; .pdbrush v1 aktarımı, restart kalıcılığı ve görselden/seçimden uç vardır. 128 preset/8 MiB maskeler/16 MiB dosya sınırı; katı doğrulama, atomik yazma, dosya kilidi/oturum çatışması ve bozuk dosya yedeği uygulanır. Koyu alanlar boyar, beyaz/saydam gizlenir; uzun kenar 512 px. Klon örneği dönmez; sadece kapsam döner. Fiziksel tablet/yazıcı testi yapılmadı. .pdeck v5 değişmedi.

Yerel `bin/photodeck` 0.9'dur; paketler `dist/applications/0.9.0-ea.1/` içindedir. Önceki paketler/başlatıcı kopyaları korunur. Sekiz yayımlanan dosyanın boyutu ve GitHub SHA-256 değeri eşleşti. Native/FUSE-free AppImage açılışı ve 206 portable payload dosya eşleşmesi başarılı. Kanıtlar `artifacts/presets-release/`, `presets-results.txt`, `presets-sanitizer-results.txt`, `presets-qmake-results.txt` ve teslimdeki `DOGRULAMA.md` içindedir.

Genel hedef Photoshop karşılaştırması **ve bütün arayüzün modernleşmesini** içerir; hedef aktiftir. Uygulanan arayüz modernleşti, fakat tam Photoshop eşdeğerliği tamamlanmadı. ABR, gelişmiş fırça dinamikleri/smoothing/simetri ve diğer açık alanlar `docs/PHOTOSHOP-COMPARISON.md` içinde devam eder.

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
