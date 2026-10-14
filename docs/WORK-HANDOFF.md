# Çalışma devir notu — 14 Ekim 2026

Kullanıcının isteğiyle geliştirme durduruldu. Yeni bir sürüm hazırlanmadı veya yayımlanmadı.

**Güncelleme:** Kullanıcı 14 Ekim'de “Devam et” diyerek çalışmayı yeniden başlattı. Aşağıdaki durdurma kaydı tarihsel bir kontrol noktasıdır; güncel çıktı çalışması test lambdasını ve native PDF yön hatasını düzeltiyor, 0.7 sürümü için doğrulanıyor.

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
