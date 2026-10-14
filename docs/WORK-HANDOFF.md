# Çalışma durumu — 14 Ekim 2026

## Güncel durum: 0.9 yayımlandı

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
