# PhotoDeck 0.11.0-ea.1 · İlk Rust erken erişimi

Ana uygulama Rust’a geçti. Hazır paketleri [GitHub Releases](https://github.com/Meingless/PhotoDeck/releases/tag/v0.11.0-ea.1) sayfasından indirin. Mimarinize uygun arşivi tamamen açın.

- Linux x86_64: Ubuntu 24.04 tabanlı paket; `chmod +x launch.sh photodeck` ardından `./launch.sh`. OpenGL, X11/Wayland ve sistem yazı tipleri gerekir. Uygulama menüsü için `./scripts/install-user.sh`.
- Windows x64: `photodeck.exe`.
- macOS: Intel veya Apple Silicon arşivindeki `PhotoDeck.app`. Uygulama ad hoc imzalıdır, noter onayı yoktur; sistemin güvenlik onayı gerekebilir.

`.pdeck` v1–v7 projeler açılır. İlk Rust sürümünde yerel tablet girdisi, bazı eski panel/görünüm ayrıntıları ve uygulama içi yazıcı diyaloğu henüz eşdeğer değildir. Yazdırma PDF üzerinden sistem görüntüleyicisini kullanır. Ayrıntılar [sürüm notlarında](docs/releases/0.11.0-ea.1.md) ve [geçiş kılavuzunda](docs/RUST-MIGRATION.md).

Kaynak derlemesi: `./scripts/build.sh`, ardından `./launch.sh`. Ana Rust kaynakları `src/`, testler `tests/`, önceki C++/Qt kaynakları `legacy/qt/` içindedir. Hazır paketin çalışması için Rust veya Qt kurulumu gerekmez.
