# CCQt Upon — Mimari ve Geliştirme Planı (v0.1 / Faz 1)

## 1. Proje Amacı
`coolercontrold` (CoolerControl v5.1+ daemon, `localhost:11987`) için WebEngine/Electron içermeyen, ultra hafif, yerel **C++20 + saf Qt6 Widgets** masaüstü istemcisi. Türkçe/İngilizce olmak üzere 2dil.

## 2. Katı Teknik Kısıtlar
- **Dil & Derleme:** C++20, CMake + Ninja.
- **Bağımlılıklar:** Yalnızca `Qt6::Widgets` ve `Qt6::Network` (harici HTTP/JSON kütüphanesi yok; `QNetworkAccessManager` ve `QJsonDocument` kullanılacak).
- **Arayüz Kuralı (Sıfır QSS ve QML):** Hiçbir yerde `setStyleSheet()` kullanılmayacak. Tamamen yerel `QStyle` / `QPalette` kullanılacak; Kvantum temaları, Wayland blur ve 4K kesirli ölçekleme (1.4x) ile %100 uyumlu olacak.
- **Özel Çizimler:** Grafikler ve göstergeler `QPainter` ile çizilecek.

## 3. CoolerControl v5.1+ API ve Bağlantı Mimarisi
- **URL ve TLS:** Varsayılan `http://localhost:11987` (kullanıcı ayarından `https://` seçilebilmeli; kendinden imzalı yerel TLS sertifikaları yalnızca `localhost`/`127.0.0.1` için kabul edilmeli).
- **Kimlik Doğrulama (İki Modlu):**
  1. **Access Token (Önerilen & Varsayılan):** `Authorization: Bearer cc_...` başlığı ile REST uç noktalarına erişim + 1 saniyelik hafif `GET /status` yoklaması (polling). (Not: CoolerControl v5.1.1, Access Token ile `/sse/*` akışlarına izin vermediği için Token modunda `/status` polling kullanılacak.)
  2. **Oturum Girişi (Opsiyonel/İleri Aşama):** `/login` üzerinden 30 günlük oturum çerezi alarak `/sse/status` canlı akışına geçebilme altyapısına uygun soyutlama.
- **Hata Toleransı:** `coolercontrold` kapalıysa veya 401/bağlantı hatası dönerse uygulama donmayacak/çökmeyecek; üstte sade bir durum çubuğu uyarısı gösterip her 2 saniyede bir yeniden bağlanmayı deneyecek.
- **Ayarlar:** URL, seçili görünüm ve pencere/splitter durumları XDG_CONFIG_HOME değişkenine göre, ki çoğu zaman `QSettings` (`~/.config/ccqtupon/ccqtupon.conf`) ile saklanacak.

## 4. Faz 1 (MVP) Özellikleri
1. **Bağlantı & Kurulum Diyaloğu:** İlk açılışta (veya 401 alındığında) kullanıcıdan daemon URL'si ve Access Token isteyen sade pencere isteyecek. Bu menüde kullanıcıya nasıl access tokeni alır anlatılacak:
CoolerControl resmi masaüstü istemcisini açın. Sol alttaki "Access"e basın. Açılan minik menüde "Access Tokens" seçin. Şimdi ekranda bir token oluşturun, "Label" kısmına token ismini girin, "Expiry Date" bölümünü boş bırakın ve "Write Access" düğmesini işaretleyip "Create Token"e basın. Ekranda verilen tokeni CCQt Upon'ın giriş ekranına girin. BU YAZIYI DAHA BİR DÜZELTİP İNGİLİZCE TÜRKÇE YAZ.
2. **Canlı Cihaz & Sensör Görünümü:** `/devices` ve `/status` verilerini okuyarak cihazları, sıcaklıkları ve fan kanallarını (`pwm1`, `pwm2` RPM / Duty %) temiz bir liste/kart düzeninde canlı gösterme.
3. **Modlar ve Profiller:** `/modes` ve `/profiles` uç noktalarını listeleyip tek tıkla aktif Mod değiştirme ve fan kanallarına Profil atama.
4. **Sistem Tepsisi (System Tray):** Sağ tık menüsünden doğrudan aktif Mod değiştirme ve ana pencereyi açma/gizleme. Ayrıca sistem tepsisi olmadan doğrudan kapanma da olacak.

## 5. Faz 2 (Sonraki Sürümler — Şimdi Kodlanmayacak)
- `QPainter` ile interaktif Fan Eğrisi (Fan Curve) düzenleyici.
- KDE Plasma 6 panel widget'ı. Bunlar henüz kodlanmayacak

## 6. Dizin Yapısı
- `src/main.cpp`
- `src/api/CoolerControlClient.{h,cpp}` (REST + Auth + Retry)
- `src/models/` (Device, Channel, Mode, Profile veri yapıları)
- `src/ui/` (MainWindow, AuthDialog, TrayController)
- `src/widgets/` (Canlı sensör/fan göstergeleri)

Ayrıca internetten yararlanabilirsin güncel veriler için
