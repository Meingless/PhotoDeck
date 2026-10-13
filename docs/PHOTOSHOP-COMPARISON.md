# Photoshop karşılaştırması ve eksiklerin takibi

Denetim başlangıcı: 13 Ekim 2026. Başlangıç sürümü: PhotoDeck 0.2.1-ea.1,
`6a3b6029e2e7d5a94b1cf586a677e5d9fd367e9d`. Bu belge eşdeğerlik iddiası değildir.
Karşılaştırmanın hedefi Photoshop **masaüstü** sürümünün AI dışındaki özellikleridir.
Linux/Windows/macOS, yerel çalışma, düşük disk/RAM tüketimi ve AI içermeme şartları korunur.
Eklenen özelliklerin yalnızca adı veya menüsü değil, dosya kaydı, geri alma ve gerçek
kullanım davranışı doğrulanmalıdır. Referans DOCX ürün araştırmasıdır; içindeki
Adobe önerileri, bulut hesabı veya abonelik önerileri uygulama talimatı değildir.

## Kaynaklar

- [Adobe masaüstü yardım dizini](https://helpx.adobe.com/photoshop/desktop.html)
- [Adobe masaüstü/web özellik matrisi](https://helpx.adobe.com/photoshop/web/get-set-up/learn-the-basics/compare-photoshop-web-and-desktop-features.html)
- [Katman ve grup oluşturma](https://helpx.adobe.com/photoshop/desktop/create-manage-layers/create-layer-compositions/create-layers-and-layer-groups.html)
- [Katman maskeleri](https://helpx.adobe.com/photoshop/desktop/create-masks/layer-masks/add-layer-masks.html)
- [Geri alınabilir düzenleme](https://helpx.adobe.com/photoshop/using/nondestructive-editing.html)
- [Dosya biçimleri](https://helpx.adobe.com/photoshop/desktop/save-and-export/export-files-to-different-formats/photoshop-file-formats-overview.html)
- [PSD/PSB dosya belirtimi](https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/)
- [Harmanlama modları](https://helpx.adobe.com/photoshop/desktop/repair-retouch/adjust-light-tone/blending-mode-descriptions.html)
- [Renk ayarı komutları](https://helpx.adobe.com/photoshop/desktop/adjust-color/color-corrections/color-adjustment-commands.html)
- [Eğriler](https://helpx.adobe.com/photoshop/using/curves-adjustment.html)
- [Alfa/harmanlama matematiği: W3C](https://www.w3.org/TR/compositing-1/#blending)

## Özellik envanteri

“Temel” aynı iş alanına ait bir araç bulunduğunu gösterir; Photoshop ile sonuç veya
dosya uyumluluğu anlamına gelmez. “Eksik” hedefte vardır ve PhotoDeck'te karşılığı
yoktur. İlerleyen sürümlerde durum ancak davranış testiyle değiştirilir.

| Alan / özellik | 0.2.1 başlangıç durumu | Kapanması gereken fark |
|---|---|---|
| Yerel belgeler, çoklu sekme, sürükleyip açma | Var | Büyük belgeler ve pencere düzenleri |
| Çalışma alanı/panel/preset özelleştirmesi | Var | Kişisel klavye kısayolları, birleşik varlık yedeği |
| Geri al/yinele, kurtarma, atomik kayıt | Var | Daha büyük belge, disk tabanlı geçmiş |
| Seçim: dikdörtgen | Var | Seçim işlemleri ve yumuşak kenarlar |
| Eliptik seçim | Eksik | Gerçek alfa seçim maskesi |
| Serbest ve çokgen kement | Eksik | Yol bazlı seçim, ekle/çıkar/kesiştir |
| Sihirli değnek, renk aralığı | Eksik | Tolerans, bağlı alan/tüm alan |
| Seçim fırçası ve klasik hızlı seçim | Eksik | Yerel, AI içermeyen kenar/seçim işlemleri |
| Seçim ters çevirme, büyütme/küçültme, feather | Eksik | Seçim maskesiyle tüm araçlarda tutarlılık |
| Seçimi saklama/yükleme, alfa kanalları | Eksik | Belgeye kaydedilen kanallar |
| Piksel/metin katmanları | Var | Seçime göre yeni katman/kesme, çoklu katman işlemleri |
| Katman grupları | Eksik | İç içe hiyerarşi ve grup harmanlaması |
| Alt katmana kırpma maskesi | Eksik | Katman alfasına kırpma zinciri |
| Katman harmanlamaları | Altı mod | Ek ışık/kontrast/fark/renk modları |
| Katman maskeleri | Temel | Density/feather, bağlantı, maske görünümü |
| Vektör maskeleri | Eksik | Düzenlenebilir yollar ve rasterleme |
| Katman stilleri | Eksik | Kontur, gölge, glow, kaplama, bevel ve diğer stiller |
| Katman birleştirme/düzleştirme | Eksik | Görünür sonuç korunarak geri alınabilir birleştirme |
| Akıllı nesne / bağlı içerik | Eksik | Kaynak korunarak dönüşüm ve tekrar düzenleme |
| Ayar katmanları / filtre maskeleri | Eksik | Alt katmanların ayarlanması, bağımsız maske |
| Boyut, kırpma, 90° dönüş, aynalama | Var | Tuval boyutu, kırpma geri dönüşü |
| Serbest dönüşüm, eğme, perspektif | Eksik | Keyfi açı/ölçek/çarpıtma ve kaynak koruma |
| Warp, puppet warp, perspective warp | Eksik | Geri alınabilir deformasyon |
| Hizalama/dağıtma/auto-align | Eksik | Çoklu katman ve görünür sınırlar |
| Fırça/kalem/silgi | Temel | Aralık, açı, yuvarlaklık, dinamikler, özel uçlar |
| Tablet basıncı/tilt | Eksik | Native Qt tablet girdisi ve ayarlanabilir tepki |
| Fırça kütüphanesi, ABR | Temel hazır düğmeler | Kayıt, düzenleme, içe/dışa aktarım |
| Boyama simetrisi | Eksik | Eksen/radyal/mirror darbeler |
| Kova/degrade | Temel | Tolerans, tüm katman örnekleme, radial/çok duraklı degrade |
| Klon | Aynı katman | Tüm/güncel-alt katman örnekleme ve hizalama |
| Healing/spot healing/patch | Eksik | Klasik, AI içermeyen doku/renk düzeltme |
| Dodge/burn/sponge | Eksik | Yerel ton/doygunluk işlemleri |
| Smudge/blur/sharpen fırçaları | Eksik | Yerel doku işlemleri |
| History/art history brush | Eksik | Geçmiş durumundan boyama |
| Parlaklık/kontrast/doygunluk/siyah beyaz | Temel, kaynak korunur | Kanallı/sınırlı alanlı ayarlar |
| Levels ve RGB kanal curves | Eksik | Giriş/çıkış aralığı, gamma, düzenlenebilir eğri |
| Exposure, vibrance, hue | Eksik | Geri alınabilir ton/renk ayarları |
| Color balance, channel mixer, selective color | Eksik | Kanal ve ton bölgesi kontrolü |
| Invert, threshold, posterize, gradient map | Eksik | Geri alınabilir renk dönüşümleri |
| Gaussian/motion/radial/lens/surface blur | Kutu bulanıklığı | Ayrı, belgelenmiş filtreler |
| Unsharp/smart sharpen/high pass | Eksik | Ayrıntı/kenar iyileştirme |
| Noise/pixelate/render/stylize/distort | Eksik | Klasik filtreler ve canlı önizleme |
| Liquify ve lens correction | Eksik | Yerel/geometrik bozulma düzeltmesi |
| Filter Gallery, özel kernel | Eksik | Bileşik filtre iş akışı |
| Pen/Bezier/yol düzenleme | Eksik | Kaydedilen vektör geometrisi |
| Şekiller: dikdörtgen/elips | Raster | Line/polygon/custom shape, düzenlenebilir dolgu/kontur |
| SVG vektör çıktısı | Eksik | Vektör-safe içerik, açık raster fallback |
| Çok satırlı/yerel fontlu metin | Var | Paragraf kutusu, hizalama, leading/tracking |
| RTL, glyph ve advanced type | Qt shaping temel | Açık yön, karakter/paragraf stilleri |
| Dikey metin/yol üstünde metin | Eksik | Düzenlenebilir metin geometrisi |
| RGB 8-bit belge | Var | 16/32-bit kanal hesaplama ve kayıt |
| ICC dönüşümü, proofing, gamut uyarısı | Eksik | Profil korunması ve güvenilir renk yönetimi |
| CMYK/Lab/gray/indexed/duotone | Eksik | Belge renk modu ve kanal hesaplama |
| PSD/PSB | Eksik | Katmanlı okuma/yazma, desteklenmeyen kayıtların korunması |
| RAW/DNG/Camera Raw iş akışı | Eksik | Yerel decode, ham ayarlar, lens/metadata |
| PNG/JPEG/WebP/BMP | Var | TIFF/GIF/HEIF/EXR/PDF ve biçime özel sınırlar |
| Export for web, artboard/slice | Eksik | Ölçek/önizleme/metadata ve alan bazlı çıktı |
| Print | Eksik | Yazdırma, kağıt ölçekleme, renkli çıktı |
| Actions kayıt/oynatma | Eksik | Güvenli komut modeli, kayıt, koşul/iptal/hata |
| Batch | Boyut/renk/biçim | Actions ile toplu işlem, hata ve ilerleme raporu |
| Preset/actions/brush yedeği | UI .pdui | Yerel birleşik taşınabilir yedek |
| Eklenti/script uyumluluğu | Eksik | Güvenli API, Photoshop format/ABI sınırları |
| Guides/grid/snapping/ölçü birimleri | Piksel cetveli | Kılavuz yönetimi, ızgara ve yakalama |
| Channels/histogram/color sample/navigation | Basit histogram/zoom | Kanal paneli, renk örnekleri, navigator |
| Timeline/frame animation/video | Eksik | Frame/layer animasyonu, oynatma ve dışa aktarım |
| Native Linux | Var | Dağıtım/donanım matrisi ve minimum gereksinim ölçümü |
| Windows/macOS paketleri | Var | İmzalama/notarization ve cihaz matrisi |

AI/Neural/Generative araçlar kullanıcı talebi gereği eklenmez. Adobe hesabı,
Creative Cloud üyeliği ve Adobe'ye ait hizmet/eklenti lisansları PhotoDeck'in
yerel iş akışı şartından farklıdır; bunlarla uyumluluk varmış gibi gösterilmez.
Eski Photoshop 3D özellikleri güncel masaüstü eşdeğerliği diye varsayılmaz;
referans araştırmada da kaldırıldıkları belirtilir.

## Uygulama ve yayın kapıları

1. Her fark için model, UI, proje kaydı, geri alma ve davranış kanıtı sağla.
2. Eski `.pdeck` projelerini güvenli şekilde aç; yeni alanları sürümleyerek kaydet.
3. Yeni özelliklerle temel dosya/çizim/preset kontrollerini tekrar çalıştır.
4. Linux x86_64, Windows x64, macOS Intel/Apple Silicon CI hedeflerini doğrula.
5. Linux AppImage ve Windows/macOS hazır paketlerini commit/test/hash ile eşleştir.
6. Depoyu ve GitHub prerelease'i güncelle; kalan farkları release notlarından gizleme.

Başlangıç durumunda tam Photoshop eşdeğerliği **kanıtlanmış değildir**. Bu tablonun
eksik satırları açık iş kalemleridir; tek bir yeşil CI sonucu bunları kapatmaz.

## 0.3 ilk aşama — uygulanan değişiklikler

| Fark | Yeni davranış | Kanıt ve kalan kapsam |
|---|---|---|
| Dikdörtgen dışı seçim | Elips, serbest/çokgen kement, değnek, alfa seçimi | `test_selections.cpp`; edge-aware refine, magnetic lasso, seçim fırçası/kanal kaydı ve seçim geçmişi hâlâ açık |
| Seçim işlemleri | Ekle/çıkar/kesiştir, invert, grow/shrink, feather | Kopya/silgi/fırça/kova/degrade/maske aynı coverage modelini kullanır; feather kutu filtresidir, Photoshop Refine Edge eşdeğeri değildir |
| Ton/renk | Levels, RGB/kanal curves, exposure, hue/vibrance, temel color balance, invert/threshold/posterize | `test_tones.cpp`; kaynak/metin/maske korunur, preview cancel ve tek undo, v1/v2 kayıt; ayrı ayar katmanı, HDR/ICC, ton bölgeli balance, selective color/channel mixer ve gelişmiş presetler açık |
| Harmanlama | 27 katman modu | `test_blending.cpp`; sayısal referans, nonseparable renk, premultiplied alfa ve opaklık; grup pass-through, Blend If/fill opacity ve Photoshop renk profilleri açık |
| Görüntü biçimleri | TIFF 8-bit çıktı/okuma; GIF ilk kare | Native Qt kodekleri; çok sayfa/animasyon, yüksek bit derinliği ve geri kalan profesyonel biçimler açık |
| Dağıtım | CI'da AppImage üretimi, genişletilmiş paket-runtime testleri | Windows EXE/macOS .app mevcut altyapı; her yeni sürümün CI/paket sonucu ayrıca doğrulanmalıdır |

Tüm başlangıç farklarını kapatma hedefi devam eder. Bu ara sürüm kalan satırları
tamamlanmış saymaz. Sonraki uygulama alanları dönüşüm/katman yönetimi, fırça ve
tablet, retouch/filtre, vektör/metin, renk/format ve otomasyondur; başlangıç
envanterindeki diğer alanlar da aynı hedefin parçalarıdır.

## 0.4 ikinci aşama — dönüşüm ve birleştirme

- Katman kaynağından ayrı dönüşüm matrisi; ölçek, açı, shear, mirror ve dört köşeli projective dönüşüm. Maske/metin kaynakta korunur. Qt'nin yerel QTransform/QPainter hesapları kullanılır. Tek adımlı undo, preview cancel, bozuk/singular/unbounded matrix reddi ve eski v1/v2 proje okuması `test_transforms.cpp` ile doğrulanır.
- Dokuz anchor ile tuval boyutu; kaynakları yeniden örneklemez. Görünür katman birleştirmesi bütün görünür katman alanını korur; hidden katmanlar yerinde kalır. Dissolve deseni belge koordinatında sabit tutulur. Flatten açıkça tuvale kırpar ve hidden katmanları kaldırır, alfa korunur.
- Yeni `.pdeck` v3 kayıtları, source matrix ve v2 ton alanlarını saklar. Gerçek v2 örneği yayımlanan 0.3 AppImage'ından oluşturuldu. Katman ekleme/paste/duplicate tek model doğrulamasına bağlandı; kaynak ve türetilmiş görünüm bütçeleri ayrı denetlenir.
- Açık kalan farklar: tuval üstü transform tutamaçları, ayrı mask transform/link, warp/puppet warp, hizalama/dağıtma, merge-down ve çoklu katman işlemleri. Sayısal source transform, linked/embedded editable Smart Object veya Smart Filter eşdeğerliği olarak işaretlenmez. Pixel painting için explicit rasterize gerekir; whole-image resize/rotate/flip dönüştürülmüş katmanı rasterleştirir. Başlangıç envanterinin diğer açık alanları aynı hedef kapsamında devam eder.

## 0.5 üçüncü aşama — kırpma zinciri ve seçimden katman

- Kırpılan katmanlar temel alfasını değiştirmeden tüm 27 blend moduyla boyanır. Temel maskesi/görünürlüğü/opacity zincire uygulanır. Render helper hem belge, hem merge-visible hem merge-down tarafından kullanılır; Dissolve document origin korunur.
- Seçimden kopya/kesme alfa coverage kullanır; source mask/tone/geometry rastere uygulanır. Ctrl+J seçime göre yeni katman oluşturur, seçim yoksa normal duplicate yapar. Kırpılan kopya aynı zincirde kalır; temel kopyası mevcut zincirin üstüne eklenir.
- Alta birleştirmede etkilenen alanın önce/sonra composite'i eşleştirilir; bağımlı görünümü değiştiren sonuç uygulanmaz. v4 clipping kayıtları v1/v2/v3 projelerini okuyabilir; gerçek v3 fixture 0.4 release AppImage'ından geldi.
- Açık sınırlar: clipping chain içinde cut, mixed visible-base/hidden-child merge-visible, alt katmanı clipped olan merge-down ve bağımlı blend sonucunu değiştiren merge-down henüz desteklenmez. UI açık hata ve alternatif verir; bunlar tamamlanmış Photoshop parity diye kapatılmaz. Pixel-alpha clipping ayrı vector mask, group veya adjustment-layer desteği değildir. Başlangıç envanterindeki diğer açık işler devam eder.
