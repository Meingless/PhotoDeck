# Geçmiş panelinde gezinme

Görünüm / paneller menüsünden Geçmiş panelini açabilirsiniz. Panel bellekte tutulan bütün adımları gösterir: en fazla 30 durum ve mevcut 128 MiB ek geçmiş bütçesi. Büyük belgelerde eski adımlar daha erken atılabilir.

- Bir adıma tıklamak o duruma doğrudan döner. Klavyede paneli Tab ile odaklayın; ok / Home / End tuşlarıyla adımı seçip Enter veya Boşluk ile uygulayın.
- Kalın yazılı `● … · Geçerli` satırı belgenin gerçek durumudur. Klavyeyle başka bir satıra bakarken bu işaret değişmez.
- `› … · Yinele` satırları ileri alınabilen durumlardır; tıklanabilir ve normal metin rengiyle okunabilir. Geçmişe dönmek ileri adımları silmez. Yeni bir düzenleme yapmak ileri dalın yerini alır.
- Adımlar katmanlarla birlikte etkin / önceki seçimi ve isimli seçim kanallarını geri getirir. Kaydedilmiş içerik noktasına dönünce değiştirilmiş işareti kalkar; yalnızca geçici seçim değişikliği bu işareti eklemez.
- Tuval dönüşümü önizlemesi uygulanmadan iptal edilir. Gönderilmemiş satır içi katman adı atılır. Diğer süren tuval hareketleri tamamlanır, sonra ilk seçilen geçmiş kimliğine gidilir. Hareket yeni bir dal oluşturup hedef ileri adımı kaldırırsa geçiş yapılmaz; durum çubuğu bunu bildirir. Güncelliğini yitirmiş bir satır başka bir duruma yönlenmez.

Bu panel kalıcı sürüm / anlık görüntü arşivi değildir. Geçmiş proje veya kurtarma dosyasına eklenmez; proje biçimi değişmez.

## Geliştirici doğrulaması

`Document::historyDescriptions()` yalnızca kopyalanmış etiket ve kararlı revizyon kimliği verir; anlık görüntülerin içeriğini dışarı açmaz. `currentHistoryIndex()` tutulan adımlar içindeki mevcut konumu belirtir. `jumpToHistoryRevision()` geçersiz / artık tutulmayan / mevcut kimlikte hiçbir değişiklik veya bildirim yapmaz. Başarılı geçiş bekleyen model düzenlemesini atar, içeriğin kaydetme kimliğini korur ve tek `updated()` bildirimi üretir. Eski `historyLabels()` mevcut adıma kadar etiket döndürmeye devam eder.

CMake hedefi `photodeck-history-navigation-tests`, CTest adı `photodeck-history-navigation`; bağımsız qmake hedefi `tests/history-navigation.pro`. Testler gerçek Qt liste fare / klavye olaylarını; seçim ve isimli kanal geri dönüşünü; kirli işareti ve ileri dalı; 25 ms yenilemeden önceki tıklamaları; kırpılmış geçmişi; sekmeleri; satır içi adı; tuval dönüşümünü ve süren seçim hareketlerini denetler. Bütün on görünüm presetinde ileri durumlar etkin, okunabilir normal metin rengiyle kontrol edilir. `PHOTODECK_HISTORY_SCREENSHOT_DIR` tanımlıysa UI testleri ekran görüntüleri kaydeder.
