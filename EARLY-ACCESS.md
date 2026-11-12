# PhotoDeck 0.11.0-ea.2 · Klasik Pro erken erişimi

Ana uygulama Rust’a geçti. Hazır paketleri [GitHub Releases](https://github.com/Meingless/PhotoDeck/releases/tag/v0.11.0-ea.2) sayfasından indirin. Mimarinize uygun arşivi tamamen açın.

- Linux x86_64: Ubuntu 24.04 tabanlı paket; `chmod +x launch.sh photodeck` ardından `./launch.sh`. OpenGL, X11/Wayland ve sistem yazı tipleri gerekir. Ubuntu 24.04 için gerekli çalışma zamanı kitaplıkları: `sudo apt install libxkbcommon-x11-0 libx11-6 libgl1 libegl1 libwayland-client0`. Uygulama menüsü için `./scripts/install-user.sh`.
- Windows x64: `photodeck.exe`. OpenGL 2.0+ destekleyen ekran sürücüsü gerekir. Yayın derlemesi C çalışma zamanını statik bağlar; ek Visual C++ Redistributable kurulumu gerektirmez.
- macOS: Intel veya Apple Silicon arşivindeki `PhotoDeck.app`. Uygulama ad hoc imzalıdır, noter onayı yoktur; sistemin güvenlik onayı gerekebilir.

`.pdeck` v1–v7 projeler açılır. Klasik Pro arayüzü ve ilerleme/iptal destekli arka plan işleri bu sürümdedir. Wayland/Windows Ink tablet girdisi ve Linux/Windows/macOS yerel yazdırma adaptörleri bulunur. X11/macOS tablet adaptörü yoktur; fiziksel tablet/yazıcı davranışı doğrulanmadı. Bazı eski panel/görünüm ayrıntıları henüz eşdeğer değildir. Ayrıntılar [sürüm notlarında](docs/releases/0.11.0-ea.2.md) ve [geçiş kılavuzunda](docs/RUST-MIGRATION.md).

Kaynak derlemesi: `./scripts/build.sh`, ardından `./launch.sh`. Ana Rust kaynakları `src/`, testler `tests/`, önceki C++/Qt kaynakları `legacy/qt/` içindedir. Hazır paketin çalışması için Rust veya Qt kurulumu gerekmez.
