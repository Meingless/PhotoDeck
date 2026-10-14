# PhotoDeck özellik doğrulama kapsamı

Bu denetim, uygulamada sunulan özellikleri kapsar. Henüz uygulanmayan Photoshop farkları [PHOTOSHOP-COMPARISON.md](PHOTOSHOP-COMPARISON.md) içinde açık hedefler olarak izlenir; testlerin geçmesi bunları kapatmaz.

| Özellik | Davranış kanıtı |
|---|---|
| Yeni belge, açma, çoklu belge, içe aktarım | `documentMenuFileWorkflow`, `multiDocumentUiAndSaveGuard`, `tabsDropFilmstripAndBrushPresets` |
| Proje kaydı / farklı kaydetme / tekrar açma | `nativeRoundTripAndExport`, `documentMenuFileWorkflow`, `transformedDocumentCanAlwaysReopen` |
| Güvenli dosya kaydı, iptal, bozuk dosya | `projectFilenameSuffixRespectsOverwriteCancellation`, `coordinateBoundsFailWithoutChangingDocument`, `hostileFilesDoNotReplaceDocument`, `multiDocumentUiAndSaveGuard` |
| PNG/JPEG/BMP/WebP ve toplu işlem | `supportedRasterFormats`, `transparencyAndJpegBackground`, `batchPreservesSourcesAndExistingOutputs`, `batchResizeColorsAndNoUpscaling` |
| Kopyala / yapıştır / seçim | `clipboardSelectionAndLayerCommands`, `allToolsRespectSelection`, `canvasSelectionShapeMoveAndCancel` |
| Katman oluşturma / çoğaltma / sıralama / ad / silme | `layerLifecycleAndOpacity`, `clipboardSelectionAndLayerCommands` |
| Opaklık / görünürlük / kilit / altı harmanlama modu | `blendModes`, `compositeMasksAndBlend`, `layerLifecycleAndOpacity`, `canvasFillCloneAndLock` |
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
| Kurtarma kopyası / yeniden açma / kaydedilmemiş belge uyarısı | `automaticRecoveryKeepsEdits`, `multiDocumentUiAndSaveGuard` |
| On preset / Odak varsayılanı / panel aç-kapa | `defaultFocusAndAllPresetsPreserveDocuments`, `allPresetAndPanelControls` |
| Kişiselleştirme / kişisel preset / içe-dışa aktarım / kalıcılık | `customAppearanceRoundTripAndHostilePreset`, `customPresetsAndSettingsSurviveRestart`, `customizationCancelRestoresLayout` |
| Yüzen panel taşıma / kilitleme / kısayollar | `focusPanelDragAndMovability`, `focusPanelKeyboardAndSettingsPersistence`, `customizationShortcutSurvivesHiddenChrome`, `gesturesFinishOnFocusChange` |
| Yardım / örnek / kapanış / yaşam döngüsü | `helpAndCloseControls`, iki test paketi; AddressSanitizer ve UndefinedBehaviorSanitizer |

Testler gerçek QWidget fare/klavye girdileri, modal pencereler, piksel karşılaştırmaları ve dosyaların yeniden okunmasıyla çalışır. Preset ve dosya varlığı tek başına davranış kanıtı kabul edilmez. CI, Linux/Windows/macOS Intel/macOS Apple Silicon hedeflerini ayrı derler; dağıtılan uygulamayı açar. Linux/Windows paketlerinde ek testler paketlenmiş Qt kütüphaneleriyle çalıştırılır. macOS'ta SDK işlev testleri ile dağıtılan `.app` açılışı ve imza doğrulaması ayrı kanıtlardır.

Masaüstü bilgisayarların tüm sürümleri veya minimum donanım koşulları bu denetimle kanıtlanmaz. Gerçek cihaz uyumluluğu için ayrıca cihaz matrisi gerekir.

0.3 ek denetimleri: `test_tones.cpp` (ton/renk, v1/v2 uyumu, hostile veri, cancel/undo, TIFF/GIF), `test_selections.cpp` (alfa seçimleri, tüm piksel araçları, kopya/maskeler, gerçek pointer/modifier/klavye girdileri), `test_blending.cpp` (27 mod, alfa/offset/opacity, deterministic dissolve, proje round-trip). Her suite CMake ve qmake ile derlenebilir. Linux/Windows CI paket testleri beş suite'i paketlenen Qt ile çalıştırır; AppImage ayrıca açılış PNG'si üretir.

0.4: `test_transforms.cpp`, source korunması, bağımsız QPainter görsel referansı, rotation/scale/shear/mirror/perspective, invalid matrix reddi, toplam render bütçesi, insertion doğrulaması, text/mask kimliği, rasterize/undo, tuval anchors, geometry sonrası image ops, off-canvas/hidden/Dissolve korunarak merge/flatten, v3 round-trip ve gerçek v2 fixture, modal preview/cancel/accept ve canvas-size UI akışını denetler. Linux/Windows paket testlerine altıncı suite eklendi. Testler bütün Photoshop farklarının kapandığı anlamına gelmez.

0.5: `test_layer_ops.cpp`, clipping alpha invariance, base mask/visibility/opacity, 27 mod sayısal tutarlılığı, bağımsız geometry, zincir/undo/release, temel silme ve sıralama, merge-visible off-canvas/Dissolve, hidden-base korunması, seçim kopya/cut/feather, aynı zincirde clipped kopya, merge-down görünüm doğrulaması/reddetme, boş piksel reddi, v4 kayıt ve gerçek v3 fixture, menü akışlarını denetler. Linux/Windows paketleri yedi suite'i çalıştırır. Desteklenmeyen karmaşık clipping/merge işlemleri karşılaştırma belgesinde açık kalır.

0.6: `test_effects.cpp`, exact Gaussian impulse kernel, sabit görüntü, gizli renk/alfa sızıntısı, premultiplied mosaic, uniform noise seed/mono/color determinism, unsharp threshold/alpha, mask density/disable/Gaussian feather/gray8 input, source/text/mask/cache/undo/rasterize, malformed params/streams, v5 kayıt ve gerçek v4 fixture, preview-off/cancel/accept/undo ve mask menu akışları. 1400×1000/radius4 yerel süre kaydı yalnızca bir senaryo ölçümüdür; minimum cihaz gereksinimi sayılmaz. Linux/Windows sekiz suite'i paketlenmiş Qt ile çalıştırır.

0.7: `test_output.cpp`, oran koruyan ortalanmış sayfa yerleşimi, beyaz kağıtta alpha, RGB ICC import/insertion, sRGB/Display P3/Adobe RGB PNG çıktısında boyut/DPI/profil ve alfa, JPEG/TIFF/WebP profil kaydı, belge değişmeme, geçersiz boyut/profil/uzantı ve PDF margin hatasında atomik dosya koruması, PDF yapısı, native fiziksel boyut/yön, QPrinter PDF aygıtı, raster iptal/kabul ve PDF gerçek modal dosya menü akışları. Linux/Windows paketleri dokuz suite'i çalıştırır. Fiziksel yazıcı çıktısı test edilmemiştir.

0.8: `test_brushes.cpp`, native Qt tablet basıncı ve eğimi, boyut/akış/tepki, basınç interpolasyonu ve hareketsiz basınç, eliptik uç, sürekli darbe aralığı/olay bölünmesi/alpha feather, bırakma noktası, kalem silgisi piksel/maske, fare bağımsızlığı, kilit/seçim/iptal/tek undo, bozuk sensör ve ayar penceresi kabul/iptal. Test aygıtları fiziksel tablet kanıtı değildir. Linux/Windows paket testlerine onuncu suite eklenir.
