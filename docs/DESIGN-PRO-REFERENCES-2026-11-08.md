# PhotoDeck — profesyonel editör referanslı taslaklar

Bu belge profesyonel editör referanslarının ve taslakların kaydıdır. Kullanıcı daha sonra **Klasik Pro** yönünü ve **mevcut PhotoDeck logosunu** seçti. Seçilen tasarım yerel Rust uygulamasına taşındı; [güncel uygulama durumu](IMPROVEMENTS-2026-11-08.md). Önceki taslaklar korunur.

## İncelenen kaynaklar

- [Adobe Photoshop çalışma alanı](https://helpx.adobe.com/photoshop/desktop/get-started/learn-the-basics/workspace-overview.html): resmî açıklama ve çalışma alanı görseli incelendi. Üstte araca bağlı ayarlar, belge sekmeleri, solda araçlar, sağda gruplanmış paneller ve tuval yakınında bağlamsal komutlar.
- [Affinity Photo — At a Glance](https://s3-eu-west-1.amazonaws.com/affinity-docs/help/photo/en-US.lproj/pages/Workspace/atAglance.html): eski resmî Photo kılavuzu ve görseli incelendi. Araç bağlam çubuğu, çalışma alanına yerleşen Studio panelleri ve altta işlem/kısayol bilgisi. Bu referans güncel Affinity sürümünün bire bir görünümü iddiası değildir.
- [GIMP 3.0 — Main Windows](https://docs.gimp.org/3.0/en/gimp-concepts-main-windows.html): resmî tek pencere görseli ve dock açıklaması incelendi. Solda araç kutusu ve araç seçenekleri, merkezde belge, sağda sekmeli kaynak/katman panelleri.

Görseller referans olarak incelendi; PhotoDeck taslaklarına üçüncü taraf logo veya ekran görüntüsü gömülmez.

## Yeni yönler

| Yön | Düzen | Referanstan alınan ilke |
| --- | --- | --- |
| Klasik Pro | Menü + araç ayarları + belge sekmeleri; solda sık araç kutusu, sağda renk/özellik ve katmanlar | Photoshop'a alışkın kullanıcı için görünür, sürekli araç erişimi |
| Bağlam Stüdyosu | Dar komut şeridi, seçili araca bağlı ayarlar, tek sütun araçlar ve sekmeli sağ paneller | Affinity Photo'nun bağlam çubuğu ve Studio düzeni |
| Çift Dok | Sol araç ızgarası ve ayrıntılı araç ayarları; sağ fırça/katman dock'u | GIMP'in araç seçeneklerini ve katmanları birlikte görünür tutması |

Ortak hedef: küçük PhotoDeck markası, daha sık yerleşim, ince ayırıcılar, cetveller, maske/katman küçük resimleri ve daha büyük yatay tuval. AI/RAW gibi mevcut olmayan işlevler eklenmiş gibi sunulmaz. Taslak etkileşimleri yalnız önizleme durumunu değiştirir.

Taslak kaynağı: `/home/omerkerem/.codex/visualizations/2026/11/08/01a1bc76-df6d-7011-9a94-2c5d9e224eab/photodeck-pro-directions.html`.

Klasik Pro seçimi uygulandı; [izin verilen yeniliklerin](IMPROVEMENTS-2026-11-08.md) tamamlanma durumları ve platform sınırları ayrı listede güncellendi. Önceki taslaklar ve kapsam için [ilk tasarım notu](DESIGN-DIRECTIONS-2026-11-08.md).

## Doğrulama

3 tasarım × 1024/736/320 px × açık/koyu tema = 18 tarayıcı görünümü kontrol edildi; taşma, gizli temel katman/opaklık kontrolü veya JavaScript hatası bulunmadı. Araç değişiminde seçeneklerin değişmesi, katman başına bağımsız opaklık, göster/gizle ve buton etiketleri ayrıca 9 etkileşim senaryosunda doğrulandı. Geniş görseller ve dar ekran örnekleri görsel olarak incelendi. Bunlar taslak testleridir; Rust uygulamasına uygulanmış özellik kanıtı değildir.
