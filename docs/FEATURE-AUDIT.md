# PhotoDeck özellik doğrulama kapsamı

Bu denetim, uygulamada sunulan özellikleri kapsar. PSD/RAW, CMYK/ICC, gelişmiş vektör yolları, tablet basıncı ve video/3D henüz uygulama özellikleri değildir.

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
