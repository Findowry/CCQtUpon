# CCQt Upon — Mimari ve Geliştirme Planı (v0.1.0 / Faz 2)

## 1. Proje Amacı
`coolercontrold` (CoolerControl v5.1+ daemon, `localhost:11987`) için WebEngine/Electron içermeyen, ultra hafif, yerel **C++20 + saf Qt6 Widgets** masaüstü istemcisi. İki dilli (**varsayılan İngilizce `en`**, menüden anlık **Türkçe `tr`** / İngilizce geçişi). MIT lisanslı.

## 2. Katı Teknik Kısıtlar
- **Dil & Derleme:** C++20, CMake + Ninja, yalnızca proje klasörü (`ccqtupon1`) içinde çalışma, tüm aşamalarda sıfır hata/uyarı ve `ctest` doğrulaması.
- **Bağımlılıklar:** Yalnızca `Qt6::Widgets` ve `Qt6::Network` (harici HTTP/JSON kütüphanesi yok; `QNetworkAccessManager` ve `QJsonDocument` kullanılacak).
- **Arayüz Kuralı (Sıfır QSS ve QML):** Hiçbir yerde `setStyleSheet()` kullanılmayacak. Tamamen yerel `QStyle` / `QPalette` kullanılacak; Kvantum temaları, Wayland blur ve 4K kesirli ölçekleme (1.4x) ile %100 uyumlu olacak.
- **Özel Çizimler:** Göstergeler, fan eğrisi düzenleyicisi, sparkline ve zaman grafikleri `QPainter` ile çizilecek.

## 3. CoolerControl v5.1.1 API, Ayarlar ve Bağlantı Mimarisi
- **URL ve TLS:** Varsayılan `http://localhost:11987` (kullanıcı ayarından `https://` seçilebilir; kendinden imzalı yerel TLS sertifikaları yalnızca `localhost` / `127.0.0.1` / `::1` için kabul edilir).
- **Kimlik Doğrulama (İki Modlu Altyapı):**
  1. **Access Token (Aktif & Varsayılan):** `Authorization: Bearer cc_...` başlığı ile REST uç noktalarına erişim + 1 saniyelik hafif `GET /status` yoklaması (polling). (Not: CoolerControl v5.1.1, Access Token ile `/sse/*` akışlarına izin vermediği için Token modunda `/status` polling kullanılır.)
  2. **Oturum Girişi (İleri Aşama Soyutlaması):** `/login` üzerinden oturum çerezi alarak `/sse/status` akışına geçebilecek arayüz.
- **Hata Toleransı:** `coolercontrold` kapalıysa veya ağ hatası dönerse uygulama donmayacak/çökmeyecek; üstte durum uyarısı gösterip her 2 saniyede bir kimliksiz `GET /handshake` ile yeniden bağlanmayı deneyecek. `401 Unauthorized` durumunda `auth_throttle` (`429`) kilidine düşmeden kullanıcı uyarılacak; salt-okunur token ile yazma işlemlerinde (`403 Forbidden`) çökmeden üst bantta uyarı gösterilecek.
- **Ayarlar (`QSettings::NativeFormat`):** `~/.config/ccqtupon/ccqtupon.conf` (`$XDG_CONFIG_HOME` uyumlu). Eski `.ini` ayar dosyası mevcutsa bir defalık okuyup `.conf` dosyasına aktarılır; token kaydedildiğinde dosya izni `0600` yapılır. Kayıtlı dil tercihi yoksa varsayılan dil **İngilizce (`en`)** açılır ve son seçilen dil `.conf` dosyasına kaydedilir.
- **Doğrudan Kapanma (Sistem Tepsisi Yok):** Sistem tepsisi (`TrayController`) ve tepsiye küçültme mantığı tamamen kaldırılmıştır; pencere kapatma (`X`) veya `Ctrl+Q` uygulamayı doğrudan ve temiz biçimde sonlandırır.

## 4. Faz 1 (Tamamlandı — Temel MVP)
1. **Bağlantı & Kurulum Diyaloğu (`AuthDialog`):** Daemon URL'si, Access Token, bağlantı testi ve düzenlenmiş İngilizce/Türkçe token alma rehberi:
   - **EN:** Open the official CoolerControl desktop app (or web interface), click **Access** in the bottom-left corner, select **Access Tokens**, enter a **Label**, leave **Expiry Date** empty, enable **Write Access**, click **Create Token**, and paste the `cc_...` token into CCQt Upon.
   - **TR:** Resmi CoolerControl masaüstü uygulamasını açın, sol alttaki **Access** düğmesine basın, **Access Tokens** seçin, **Label** alanına bir isim girin, **Expiry Date** alanını boş bırakın, **Write Access** seçeneğini işaretleyip **Create Token**'a basın ve verilen token'ı CCQt Upon giriş ekranına yapıştırın.
2. **Canlı Cihaz & Sensör Görünümü:** `/devices`, `/status`, `/settings/overrides` verilerini okuyarak cihazları, sıcaklıkları ve kanalları canlı gösterme.
3. **Modlar ve Profiller:** `/modes`, `/modes-active`, `/profiles`, `/devices/{uid}/settings` uç noktaları üzerinden tek tıkla Mod değiştirme ve kanallara Profil atama.

## 5. Faz 2 (Aktif Kapsam — v0.1.0)
### 5.1. Altyapı ve Arayüz İyileştirmeleri
- `QSettings::NativeFormat` (`.conf`) geçişi ve tek seferlik `.ini → .conf` aktarımı (`0600` izinli).
- İlk açılışta varsayılan dil İngilizce (`en`); menüden anlık `en`/`tr` seçimi.
- `TrayController` sınıfının, tepsi menüsünün ve tepsiye küçültme mantığının tamamen kaldırılması.
- **`Help → About` (Yardım → Hakkında) Diyaloğu:** Yerel `QDialog` içinde `CCQt Upon v0.1.0`, kısa açıklama, MIT lisansı, Qt6 sürümü ve bağlı `coolercontrold` sürümü.
- **Profil `QComboBox` İyileştirmesi:** Cihaz kartlarında uzun profil adlarının (ör. `Laptop Fan (Silent, Calibrated) cpu`) kesilmemesi ve tam profil adını gösteren dinamik `setToolTip()`.
- **"Open in CoolerControl" / "CoolerControl'de Aç" Düğmesi:** Üst araç çubuğunda `Connection Settings` düğmesinin hemen sağında yer alır; `QProcess::startDetached("coolercontrol", {})` ile resmi masaüstü uygulamasını başlatır, bulunamazsa `QDesktopServices::openUrl` ile kayıtlı daemon URL'sini tarayıcıda açar.

### 5.2. Üst Sekme Mimarisi (`QTabWidget`)
Ana pencere `SightProc` tarzında üstte `QTabWidget` yapısına geçer:
1. **Overview / Genel Bakış:** Mevcut cihaz/sensör/mod/profil görünümü.
2. **Cooling / Soğutma:** Tam teşekküllü fan kontrol ve eğri düzenleme sekmesi:
   - **Sol Panel:** Kontrol edilebilir fan kanalları listesi (`hp` altında `Fan1`, `Fan2` vb., her satırda canlı `Duty %` ve `RPM`), altında kayıtlı `Profiles` ve `Functions` (`GET /functions`) listesi.
   - **Sağ Çalışma Alanı (Seçili Fan İçin):**
     - Üst şeritte seçili fan adı, canlı `Duty %` ve `RPM` değeri, mod seçici (**`Profile` | `Manual` | `Unmanaged`**) ve sağda **`Apply` (Uygula)** düğmesi.
     - **`Manual` Modu:** `0–100%` `QSlider` + `QSpinBox` (`- % +`); merkezde `QPainter` ibreli gösterge (`FanGaugeWidget`: canlı hız ve hedef manuel hız ibreleri + ortada büyük `%` ve `RPM`); altta seçili fanın son 60–120 saniyelik `Duty %` ve `RPM` zaman grafiği (`FanHistoryChartWidget`); `Apply` ile `PUT /devices/{uid}/settings/{ch}/manual` (`{"speed_fixed": <uint8>}`).
     - **`Profile` Modu:** Atanacak profili seçme (`QComboBox`), profil türünü (`Graph`/`Fixed`/`Default`/`Mix`/`Overlay`), sıcaklık kaynağını ve bağlı fonksiyonu (`Functions`) gösterme; `Graph` profilleri için **`QPainter` İnteraktif Fan Eğrisi Düzenleyicisi (`FanCurveEditorWidget`)** (X: sıcaklık `°C`, Y: `Duty %`, anlık sıcaklık dikey kesikli çizgi, anlık duty yatay çizgi, sürükle-bırak noktalar, çift tıkla nokta ekleme, sağ tıkla silme) + yanında/altında grafikle çift yönlü senkron çalışan **Points (`Temp °C`, `Duty %`)** tablosu; `Apply` ile `PUT /profiles` (değişiklik varsa, gerçek v5.1.1 `Profile` JSON şemasıyla) ve `PUT /devices/{uid}/settings/{ch}/profile` (`{"profile_uid": "..."}`).
     - **`Unmanaged` Modu:** Fanı donanım/BIOS varsayılanına döndüren açıklama ve `Apply` ile `PUT /devices/{uid}/settings/{ch}/reset`.

## 6. Faz 3 (Tamamlandı — Bağımsız KDE Plasma 6 Widget'ı)
- `src/` kodundan tamamen bağımsız, saf Plasma 6 QML (`Kirigami` / `PlasmaComponents3` / `XMLHttpRequest`) widget paketi: `plasmoid/org.ccqtupon.fan.plasmoid/`.
- Panelde 3 görünüm modu (`Icon only`, `Fan RPM`, `Fan Duty (%)`), açılır pencerede `Profile | Manual | Unmanaged` modları ve minik `Canvas` eğri önizlemesi; `401` durumunda throttle koruması ve çevrimdışı durumda `/handshake` yeniden bağlanma.

## 7. Dizin Yapısı (v0.1.0)
- `src/main.cpp`
- `src/api/CoolerControlClient.{h,cpp}` (REST + Auth + Retry + Functions/Manual/Profile PUT)
- `src/models/CoolerControlModels.{h,cpp}` (`Device`, `Channel`, `Mode`, `Profile`, `Function`, `NameOverrides`, `serializeProfile`)
- `src/ui/` (`MainWindow`, `CoolingTabWidget`, `AuthDialog`, `AboutDialog`, `AppTranslator`)
- `src/widgets/` (`StatusBanner`, `MetricBarWidget`, `DeviceCardWidget`, `FanGaugeWidget`, `FanHistoryChartWidget`, `FanCurveEditorWidget`)
- `packaging/ccqtupon.desktop`
- `plasmoid/org.ccqtupon.fan.plasmoid/` (`metadata.json`, `contents/config/{main.xml,config.qml}`, `contents/ui/{main.qml,configGeneral.qml,MiniCurveCanvas.qml}`)
- `tests/ParserTests.cpp`
