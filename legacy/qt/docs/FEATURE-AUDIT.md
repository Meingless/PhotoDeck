# PhotoDeck özellik doğrulama kapsamı

Bu denetim, **0.10.0-ea.4** kaynak adayının davranış ve test kapsamını gösterir. Test adı bulunması testin bu commit'te geçtiği anlamına gelmez; çalıştırılan kaynak, ortam ve sonuçlar [0.10 geliştirme notlarında](DEVELOPMENT-0.10.md) ayrı kaydedilir. Aşağıdaki 0.2–0.9.1 sonuçları tarihseldir; yeni dalın paket veya CI kanıtı değildir. Henüz uygulanmayan Photoshop farkları [PHOTOSHOP-COMPARISON.md](PHOTOSHOP-COMPARISON.md) içinde açık hedefler olarak izlenir; testlerin geçmesi bunları kapatmaz.

## 0.10.0-ea.4 — güncel kapsam

| Davranış | Kaynak testleri ve kullanım sınırları |
|---|---|
| Gömülü akıllı nesneye dönüşüm, bağımsız kimlikle çoğaltma, tam çözünürlüklü içerik değiştirme ve açık rasterleştirme | `tests/test_smart_objects.cpp`; [akıllı nesne kılavuzu](SMART-OBJECTS.md). Bağlantılı dosya, iç içe belge, ayrı içerik sekmesi veya yeniden sıralanabilir akıllı filtre yığını değildir. Maske/efekt işleme sabit çerçeve çözünürlüğündedir. |
| Akıllı kaynağı boyama/kesme/bütün-görüntü dönüşümü/birleştirmeden koruma; v7 kayıt, kurtarma ve ilk kayıt uyarısı | `tests/test_smart_objects.cpp`, `tests/test_features.cpp`, `tests/test_output.cpp`, `tests/test_ui_resilience.cpp`. EA2 ve eski okuyucular v7'yi açamaz; akıllı nesne yoksa v5/v6 korunur. |
| Konum/biçim/kalite/boyut/DPI/ICC ile tek ekranda raster çıktı; iptal, üzerine yazma ve açık dönüşüm önizlemesini koruma | `tests/test_export_dialog.cpp`; [dışa aktarım kılavuzu](EXPORT.md). Yeni kodlayıcı, CMYK, soft-proof veya katmanlı çıktı eklemez. |
| Tuvalden hedef seçimi, canlı dönüşüm ölçüleri, açık oran kilidi ve kaynak koruyan boyamaya geçiş | `tests/test_usability.cpp`; [dönüşüm kılavuzu](ON-CANVAS-TRANSFORM.md). Seçimi kopyala ve dönüştür ayrı raster kopya üretir. |
| Seçim geri alma/yeniden seçme ve açıkça adlandırılmış kayıtlı seçimler | `tests/test_selection_workflow.cpp`, `tests/test_ui_resilience.cpp`; [seçim ve biçim kılavuzu](SELECTION-WORKFLOW.md) |
| Ctrl+T ile bütün etkin katmanda tuval üstü taşıma/ölçek/dönüş; Ctrl+Alt+T ile sayısal/perspektif ayarları | `tests/test_transforms.cpp`; [dönüşüm kılavuzu](ON-CANVAS-TRANSFORM.md). Seçili piksel parçası dönüşümü veya warp değildir. |
| F2 ile satır içi katman adı; tıklanan satırı hedefleyen bağlam menüsü | `tests/test_layer_ops.cpp`: `inlineRenamePreservesSourcesAndIsOneUndo`, `layerContextMenuTargetsClickedRowAndCanCancel` |
| Liste içinde tek katmanı sürükleyerek sıralama, görünür bırakma çizgisi, iç/dış kenar kaydırması ve güvenli iptal | `tests/test_layer_drag.cpp`, `tests/test_layer_ops.cpp`; [katman kılavuzu](LAYER-WORKFLOW.md). Çoklu seçim, dışarı taşıma veya belge arası bırakma değildir. |
| Sayısal X/Y, tuval/seçim sınırına yatay-dikey hizalama; odaklı tuvalde Taşı aracıyla ok/Shift+ok = 1/10 belge pikseli | `tests/test_positioning.cpp`; [konumlandırma kılavuzu](LAYER-POSITIONING.md). Çoklu katman hizalama/dağıtım değildir. |
| Geçmiş panelinde geri ve ileri adımlara fare/klavyeyle gezinme | `tests/test_history_navigation.cpp`; [geçmiş kılavuzu](HISTORY-NAVIGATION.md). Kalıcı geçmiş arşivi değildir. |
| Arka planda kurtarma kodlama/yazımı; süreç genelinde en fazla bir değişmez anlık görüntü ve kodlayıcı | `tests/test_recovery.cpp`, `tests/test_recovery_files.cpp`, `tests/test_recovery_codec.cpp`; [kurtarma kılavuzu](RECOVERY.md). Normal Kaydet eşzamanlı kalır. |

Akıllı nesne içeren normal proje/kurtarma v7 kullanır; ilk v7 kaydı uyumsuzluğu açıklar ve eski özgünü ayrı kopyayla korur. Son nesne silinir/rasterleştirilirse düşük sürüme dönülebilir. Akıllı nesne yoksa normal proje kaydı, adlandırılmış seçim yoksa v5'tir. Adlandırılmış seçimler v6 gerektirir; yayımlanan 0.9.1 bunları açamaz. İlk v6 kaydında uyarı ve eski orijinali ayrı kopyayla koruyan akış vardır. Yalnız geçici seçim v6 yükseltmesi yapmaz; kurtarma bağlamının ayrı v6 kullanımı seçim kılavuzunda açıklanır. Kayıtlı seçimler genel amaçlı bir alfa Channels paneli değildir. PSD/RAW, CMYK/yüksek bit, gruplar, ayar katmanları ve gelişmiş Photoshop eşdeğerliği bu kapsamda eklenmedi.

## Genel davranış regresyonları

| Özellik | Davranış kanıtı |
|---|---|
| Yeni belge, açma, çoklu belge, içe aktarım | `documentMenuFileWorkflow`, `multiDocumentUiAndSaveGuard`, `tabsDropFilmstripAndBrushPresets` |
| Proje kaydı / farklı kaydetme / tekrar açma | `nativeRoundTripAndExport`, `documentMenuFileWorkflow`, `transformedDocumentCanAlwaysReopen` |
| Güvenli dosya kaydı, iptal, bozuk dosya | `projectFilenameSuffixRespectsOverwriteCancellation`, `coordinateBoundsFailWithoutChangingDocument`, `hostileFilesDoNotReplaceDocument`, `multiDocumentUiAndSaveGuard` |
| Dış değişiklik / silinme / aynı boyut-zamanlı değişiklik / yol takma adları | `test_save_safety.cpp`: `independentEditorCannotBeSilentlyOverwritten`, `hashDetectsSameSizeSameTimestampInPlaceEdit`, `deletionNeedsExplicitOverwriteAndRecoveryKeepsConflict`, `canonicalAliasesCannotBypassConflict`, `symbolicAliasesAndRetargetingAreProtected`; UI: `externalConflictCancelThenExplicitOverwrite` |
| v1–v4 orijinali koruyarak kopya kaydetme (adlandırılmış seçim yoksa v5) | `legacyOriginalRequiresConsent`, `externalLegacyEditRequiresBothIndependentPermissions`; UI: `oldProjectsSaveAsCopy` |
| PNG/JPEG/BMP/WebP ve toplu işlem | `supportedRasterFormats`, `transparencyAndJpegBackground`, `batchPreservesSourcesAndExistingOutputs`, `batchResizeColorsAndNoUpscaling` |
| Kopyala / yapıştır / seçim | `clipboardSelectionAndLayerCommands`, `allToolsRespectSelection`, `canvasSelectionShapeMoveAndCancel` |
| Katman oluşturma / çoğaltma / sıralama / ad / silme | `layerLifecycleAndOpacity`, `clipboardSelectionAndLayerCommands` |
| Opaklık / görünürlük / kilit / 27 harmanlama modu | `blendModes`, `compositeMasksAndBlend`, `layerLifecycleAndOpacity`, `canvasFillCloneAndLock` |
| Maske oluşturma / seçime göre maske / ters çevirme / kaldırma | `maskSelectionInvertRemoveAndUndo`, `existingLayerMaskAtLayerLimit`, `clipboardSelectionAndLayerCommands` |
| Maskede metin kimliğinin ve kaynak piksellerin korunması | `allMasksKeepTextEditable`, `textEditingPreservesMask` |
| Taşı / fırça / kalem / silgi / klon / kova / degrade | `finalPointerPositionIsApplied`, `singleBrushStampRespectsFlow`, `eraserIsIndependentOfPickedAlpha`, `canvasFillCloneAndLock`, `connectedFillDoesNotCrossBorder`, `fillFlowBlendsOverExistingPixels` |
| Dikdörtgen / elips / Shift ile kare-daire | `shapesAndShiftConstraint`, `allToolsRespectSelection` |
| Metin / çok satır / Türkçe / italik / font ve boyut | `textAndColorDialogs`, `textPadding`, `nativeRoundTripAndExport` |
| Damlalık / renk seçici / renk değişimi | `pickerSeesLastPixelAndZoomKeepsAnchor`, `textAndColorDialogs` |
| Kırpma / yeniden boyutlandırma / dönüş / aynalama | `transformsKeepLayerAlignment`, `transformsAndMasksRoundTrip`, `filterTransformAndViewCommands` |
| Parlaklık / kontrast / doygunluk / siyah-beyaz / bulanıklık | `nonDestructiveColorExtremes`, `adjustmentsAndBlurPreserveSource`, `filterTransformAndViewCommands` |
| Zoom / gerçek boyut / sığdır / tuval gezinmesi | `pickerSeesLastPixelAndZoomKeepsAnchor`, `handPanIncludesMouseRelease`, `filterTransformAndViewCommands` |
| Geri al / yinele / kaydetme noktası / bellek sınırı | `historyAndSavePoint`, `historyMemoryBudget`, `idleClicksDoNotConsumeUndo`, `largeProjectsRetainIncrementalUndo` |
| Kurtarma kopyası / yeniden açma / kaydedilmemiş belge uyarısı | `automaticRecoveryKeepsEdits`, `multiDocumentUiAndSaveGuard`, `earlyRecoveryAndDeferredGesture`, `recoveredCopyCanReuseSnapshotWithoutAdoptingItAsProject` |
| Araç ve sekmeye bağlı seçenekler / tuvali kapatmayan Odak panelleri | `toolOptionsFollowToolsAndDocumentTabs`, `focusPanelsLeaveCanvasUncovered` (900×620 ve 1280×800); araç erişilebilir adları ve Ctrl+Alt+1…0 eşlemeleri |
| Değişmeyen küçük resimleri kullanma / canlı harekette önizlemeyi erteleme | `previewsReuseUnchangedPixelsAndDeferLiveStroke` |
| On preset / Odak varsayılanı / panel aç-kapa | `defaultFocusAndAllPresetsPreserveDocuments`, `allPresetAndPanelControls` |
| Kişiselleştirme / kişisel preset / içe-dışa aktarım / kalıcılık | `customAppearanceRoundTripAndHostilePreset`, `customPresetsAndSettingsSurviveRestart`, `customizationCancelRestoresLayout` |
| Yüzen panel taşıma / kilitleme / kısayollar | `focusPanelDragAndMovability`, `focusPanelKeyboardAndSettingsPersistence`, `customizationShortcutSurvivesHiddenChrome`, `gesturesFinishOnFocusChange` |
| Yardım / örnek / kapanış / yaşam döngüsü | `helpAndCloseControls`, `aboutAndHelpMatchCurrentBehavior`; sürüm kaynağı `src/version.h`; ek bellek/UB denetimi için aşağıdaki sanitizer akışı |
| Proje ve fırça parser regresyonları | `test_parser_safety.cpp`: tüm kesme noktaları, bozuk uzunluklar, sabit tohumlu byte mutasyonları, reddedilen girdide hedef/geçmiş korunması ve geçerli kayıt round-trip kontrolleri |

Testler gerçek QWidget fare/klavye girdileri, modal pencereler, piksel karşılaştırmaları ve dosyaların yeniden okunmasıyla çalışır. Preset ve dosya varlığı tek başına davranış kanıtı kabul edilmez. Aşağıdaki sürüm notları tarihsel kapsam kaydıdır; yeni kaynak değişikliklerinin testten geçtiğini kendiliğinden göstermez.

## Rapor düzeltmelerinin sınırları

- Kurtarma, tamamlanan düzenlemelerden yaklaşık 5 saniye sonra planlanır; 45 saniyelik periyodik kontrol yedektir. Devam eden çizim/taşıma hareketinde yazılmaz; ertelenen kontrol hareket tamamlandığında hemen yeniden denenir. Timer testleri gerçek 5/45 saniye beklemek yerine aralıkları ve hızlandırılmış timeout akışını doğrular. Bu davranış sıfır veri kaybı garantisi değildir.
- 0.10 geliştirme dalında kurtarma, değişmez anlık görüntüyü arka planda kodlayıp yazar; aynı süreçte tek anlık görüntü/kodlayıcı sınırı vardır. GUI'de yakalama ve son atomik yol yayımı kalır; bütün depolama gecikmelerinin yokluğu iddia edilmez. Normal Kaydet hâlâ eşzamanlı PNG kodlama/dosya yazımı kullanır. Yaşam döngüsü ve bellek sınırları [kurtarma kılavuzundadır](RECOVERY.md); küçük test süreleri düşük özellikli cihaz ölçümü değildir.
- Dış değişiklikler dosya kimliği, boyut/zaman ve SHA-256 özetiyle kontrol edilir. UI iptal, ayrı kopya veya açıkça üzerine yazmayı sunar. Kontrol iyimseldir; kontrol ile atomik commit arasındaki yarış aralığı bütünüyle kapatılmış değildir.
- Eski v1–v4 orijinali UI üzerinden yerinde yükseltilmez; uyarıdan sonra farklı bir kopya seçilir (adlandırılmış seçim yoksa v5, varsa v6). Modelin açık seçenekleri ile UI'nin kullanıcıya sunduğu kopya akışı ayrı test edilir.

## Sürekli doğrulama ve sınırları

- `.github/workflows/validate.yml`: pull request ve `main` push'unda Linux/Windows/macOS Intel/macOS Apple Silicon için Qt 6.8.3 CMake derlemesi ve tüm CTest grupları; JUnit raporları ve CTest günlükleri artifacts olarak saklanır.
- Aynı akışın Linux Clang ASan/UBSan işi uygulama ve testleri instrument eder. Önceden derlenmiş Qt SDK'sı instrument edilmez; leak detection kapalıdır. Bulgu test işini başarısız yapar.
- `photodeck-parser-safety` küçük geçerli dosyalardan her kesme noktasını ve 256'şar sabit tohumlu mutasyonu üretir. Reddedilen proje girdisinde katmanlar, seçim, dosya kimliği ve kullanılabilir undo/redo; fırça girdisinde hedef koleksiyon korunmalıdır. Kabul edilen mutasyonlar yeniden kaydedilip açılmalıdır. Görüntü okuyucu tahsis sınırı bu testte 32 MiB'dir. Bu sınırlı corpus, kapsam güdümlü fuzzing veya bütün parser açıklarının yokluğu iddiası değildir.
- `.github/workflows/build.yml` etiket/manuel erken erişim paketleme akışı olarak korunur. Linux/Windows paketlerinde testler paketlenmiş Qt kütüphaneleriyle çalıştırılır; macOS'ta SDK testleri, `.app` açılışı ve ad-hoc imza bütünlüğü ayrı adımlardır. Ad-hoc imza kontrolü dağıtım sertifikası veya notarization değildir.
- Yeni PR akışı ve parser testi için sonuç ancak çalıştırılan commit'e ait loglarla doğrulanabilir. İş tanımı veya tarihsel sürüm sonucu tek başına yeni dal için başarılı test kanıtı sayılmaz.

## Tarihsel 0.9.1 doğrulama durumu — 15 Ekim 2026

| Kontrol | Ortam / kapsam | Sonuç |
|---|---|---|
| Değişiklik öncesi temiz temel | `a2aa2ed`, Linux x86_64, Qt 6.8.3, offscreen | CTest 11/11 geçti; 7,30 sn. Bu sonuç yeni dalı doğrulamaz. |
| Son Release kaynak ağacı | `fix/report-modern-workspace`, Linux x86_64, Qt 6.8.3, offscreen; araç-bağlamı/panel ekleri dahil | CMake derlemesi ve CTest 14/14 geçti; 11,40 sn. Toplam 379 Qt Test kontrolü (başlangıç/bitiş dahil), 351 test satırı. Yeni UI grubu 15, kayıt grubu 17, parser grubu 5 başarılı kontrol. |
| İş tanımının statik kontrolü | YAML okuma; PR/main tetikleyicileri; dört hedef; salt okunur izinler; gömülü shell sözdizimi | Geçti; GitHub işi çalıştırılması değildir. |
| Son yerel Linux ASan/UBSan | GCC 14.2, Debug, Qt 6.8.3; uygulama + testler instrument edildi; Qt SDK instrument edilmedi, leak detection kapalı | CTest 14/14 geçti; 25,65 sn. GitHub Clang işi için kanıt değildir. |
| qmake uygulama derlemesi / sürüm / açılış | Yalıtılmış Qt/OpenGL sysroot; OpenGL için açık LIBS arama yolu | Derleme, `--version` (`0.9.1-ea.1`) ve başlangıç PNG üretimi geçti. Bu ortamın bağlantı ayarı standart sistem Qt kurulumunu temsil etmez. |
| Yerel Linux taşınabilir 0.9.1 paketi | Yalnız paket kütüphaneleriyle bütün CTest envanteri; geçici test dosyaları arşive girmez | 14/14 grup, 379 Qt kontrolü; hata/skip yok. Paket sürümü, açılış PNG’si ve arşiv içeriği doğrulandı. GitHub-hosted dört platform paketinin yerine geçmez. |
| Görsel ve etkileşim kontrolü | On preset; Odak 900×620 ve 1280×800; Fırça/Sihirli değnek/Kova; kaydırılmış özellikler | Kontroller geçti; uyarı görülmedi. Fiziksel cihaz matrisi değildir. |
| İlk dört platform PR CI | [Run 38736782571](https://github.com/Meingless/PhotoDeck/actions/runs/38736782571), kaynak `5c4609b` | Dört hedef derlendi. Windows ve Clang ASan/UBSan 14/14 geçti. Linux Release ve iki macOS hedefinde yalnızca UI diyalog otomasyonu başarısız oldu. Dosya adının gerçek alana yazılması ve modeless About desteğiyle test sürücüsü düzeltildi; güncel commit sonucu [PR kontrollerinden](https://github.com/Meingless/PhotoDeck/pull/1/checks) okunmalıdır. |
| Fiziksel cihaz / imzalı dağıtım | Tablet, yazıcı, gerçek düşük özellikli cihazlar; dağıtım sertifikası/notarization | Bu çalışmada doğrulanmadı. |

[İkinci PR çalıştırmasında](https://github.com/Meingless/PhotoDeck/actions/runs/38737997410) (`0db4fd8`) Linux Release de 14/14 geçti; macOS Hakkında kontrolü, Qt’nin pencere başlığını yok sayması nedeniyle sürümün görünür içerikte eksik olduğunu ortaya çıkardı. Sürüm artık Hakkında gövdesinde de gösterilir ve içerik tüm platformlarda ayrı doğrulanır.

Yerel sonuçlar o tarihte doğrulanan kaynak ağacına, GitHub sonuçları belirtilen run/commit’e aittir. Yeni dal, yeni yayımlanmış paket veya fiziksel cihaz kanıtı değildir.

## Tarihsel 0.9.1 yerel performans karşılaştırması — 15 Ekim 2026

Üç eşleştirilmiş başlangıç/son çalıştırmada 4096×4096, üç katman ve 192 MiB kaynak görüntü kullanıldı. Aşağıdaki değerler her çalıştırmanın medyanlarının medyanıdır; hızlandırma iddiası yalnızca bu sentetik yerel senaryoya aittir.

| İşlem | Başlangıç | Son |
|---|---:|---:|
| Değişmeyen içerikte yardımcı arayüz yenilemesi | 85,726 ms | 0,309 ms |
| Zorlanan sentetik taşıma + tuval boyama + yardımcı yenileme | 122,446 ms | 42,727 ms |

Son kompozisyon ve filmşeridi hash'leri eşleşti; değişmeyen simgeler 36/36 kontrolde yeniden kullanıldı. Yaklaşık 43 ms'lik ikinci yol 60 fps garantisi değildir. O tarihsel kaynakta PNG kodlama/dosya yazımı eşzamanlıydı; bu ölçüm kayıt/kurtarma beklemelerini ortadan kaldırdığını göstermez. 0.10'un arka plan kurtarma ölçümleri ve sınırları [geliştirme notlarında](DEVELOPMENT-0.10.md) ayrıdır. Kaynak rapor: `artifacts/perf/report.md` (ayrı doğrulama tesliminin parçası).

Masaüstü bilgisayarların tüm sürümleri veya minimum donanım koşulları bu denetimle kanıtlanmaz. Gerçek cihaz uyumluluğu için ayrıca cihaz matrisi gerekir.

## Tarihsel 0.3–0.9 ek denetimleri

Bu bölüm, ilgili sürümün eklendiği zamanki kapsamı ve paket akışını kaydeder. Güncel 0.10 kaynak test envanteri ve sonuçları yukarıdaki geliştirme bağlantılarından ayrı izlenir.

0.3 ek denetimleri: `test_tones.cpp` (ton/renk, v1/v2 uyumu, hostile veri, cancel/undo, TIFF/GIF), `test_selections.cpp` (alfa seçimleri, tüm piksel araçları, kopya/maskeler, gerçek pointer/modifier/klavye girdileri), `test_blending.cpp` (27 mod, alfa/offset/opacity, deterministic dissolve, proje round-trip). Her suite CMake ve qmake ile derlenebilir. Linux/Windows CI paket testleri beş suite'i paketlenen Qt ile çalıştırır; AppImage ayrıca açılış PNG'si üretir.

0.4: `test_transforms.cpp`, source korunması, bağımsız QPainter görsel referansı, rotation/scale/shear/mirror/perspective, invalid matrix reddi, toplam render bütçesi, insertion doğrulaması, text/mask kimliği, rasterize/undo, tuval anchors, geometry sonrası image ops, off-canvas/hidden/Dissolve korunarak merge/flatten, v3 round-trip ve gerçek v2 fixture, modal preview/cancel/accept ve canvas-size UI akışını denetler. Linux/Windows paket testlerine altıncı suite eklendi. Testler bütün Photoshop farklarının kapandığı anlamına gelmez.

0.5: `test_layer_ops.cpp`, clipping alpha invariance, base mask/visibility/opacity, 27 mod sayısal tutarlılığı, bağımsız geometry, zincir/undo/release, temel silme ve sıralama, merge-visible off-canvas/Dissolve, hidden-base korunması, seçim kopya/cut/feather, aynı zincirde clipped kopya, merge-down görünüm doğrulaması/reddetme, boş piksel reddi, v4 kayıt ve gerçek v3 fixture, menü akışlarını denetler. Linux/Windows paketleri yedi suite'i çalıştırır. Desteklenmeyen karmaşık clipping/merge işlemleri karşılaştırma belgesinde açık kalır.

0.6: `test_effects.cpp`, exact Gaussian impulse kernel, sabit görüntü, gizli renk/alfa sızıntısı, premultiplied mosaic, uniform noise seed/mono/color determinism, unsharp threshold/alpha, mask density/disable/Gaussian feather/gray8 input, source/text/mask/cache/undo/rasterize, malformed params/streams, v5 kayıt ve gerçek v4 fixture, preview-off/cancel/accept/undo ve mask menu akışları. 1400×1000/radius4 yerel süre kaydı yalnızca bir senaryo ölçümüdür; minimum cihaz gereksinimi sayılmaz. Linux/Windows sekiz suite'i paketlenmiş Qt ile çalıştırır.

0.7: `test_output.cpp`, oran koruyan ortalanmış sayfa yerleşimi, beyaz kağıtta alpha, RGB ICC import/insertion, sRGB/Display P3/Adobe RGB PNG çıktısında boyut/DPI/profil ve alfa, JPEG/TIFF/WebP profil kaydı, belge değişmeme, geçersiz boyut/profil/uzantı ve PDF margin hatasında atomik dosya koruması, PDF yapısı, native fiziksel boyut/yön, QPrinter PDF aygıtı, raster iptal/kabul ve PDF gerçek modal dosya menü akışları. Linux/Windows paketleri dokuz suite'i çalıştırır. Fiziksel yazıcı çıktısı test edilmemiştir.

0.8: `test_brushes.cpp`, native Qt tablet basıncı ve eğimi, boyut/akış/tepki, basınç interpolasyonu ve hareketsiz basınç, eliptik uç, sürekli darbe aralığı/olay bölünmesi/alpha feather, bırakma noktası, kalem silgisi piksel/maske, fare bağımsızlığı, kilit/seçim/iptal/tek undo, bozuk sensör, boyama dışı tablet olaylarında Qt fare benzetiminin korunması ve ayar penceresi kabul/iptal. Test aygıtları fiziksel tablet kanıtı değildir. Linux/Windows paket testlerine onuncu suite eklenir.

0.9: `test_presets.cpp`, tüm parametre/görsel maskenin .pdbrush round-trip'i, tip koyuluk/alfa/oran, katı alan/sürüm/bütçe/isim doğrulaması ve dosya koruması, renk-cache/dönüş/undo, klonun kapsam dışında piksel bırakmaması ve örneğin dönmemesi, görünmez darbede metin kimliği, gerçek preset kaydet/kullan/import/export/seçimden uç akışları, sekme ayrılığı, restart, dosya çakışması ve bozuk dosya yedeği, özel yüzey metninde kontrast. On UI presetinin gerçek masaüstü başlangıç görüntüleri ve menü/kütüphane/ayar diyalogu PNG'leri ayrıca kontrol edilir. Linux/Windows paketleri on bir suite'i çalıştırır.

## Tarihsel 0.10.0-ea.1 — etkin katman konumu test ayrıntısı

`test_positioning.cpp`: altı hizalama komutu × tuval/seçim hedefleri × dört geometri; sayısal X/Y, tek adımlı undo/redo, no-op yineleme koruması, tek sayılı/tuvalden büyük merkezleme, kilit/görünürlük/boş seçim/geçersiz sınır reddi, metin/maske/geometri ve seçim koruması, v5/v6 tekrar açma. Gerçek widget testleri menü hedeflerini, bekleyen hareketi, sayısal kabul/iptal/eski bağlamı ve on presette 900×620 yerleşimi kapsar. Sınırın geometrik anlamı ve çoklu katman/dağıtım kapsamı [LAYER-POSITIONING.md](LAYER-POSITIONING.md) içinde açıklanır.
