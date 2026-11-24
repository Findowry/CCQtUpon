#include "ui/AppTranslator.h"

#include <QCoreApplication>
#include <QLocale>

namespace ccqt {

AppTranslator::AppTranslator(QObject* parent)
    : QTranslator(parent)
{
    initTurkishDictionary();
}

Language AppTranslator::language() const
{
    return m_language;
}

void AppTranslator::setLanguage(Language lang)
{
    if (m_language == lang) {
        if (lang == Language::Turkish && QCoreApplication::instance()) {
            QCoreApplication::removeTranslator(this);
            QCoreApplication::installTranslator(this);
        }
        return;
    }

    m_language = lang;
    if (QCoreApplication::instance()) {
        QCoreApplication::removeTranslator(this);
        if (m_language == Language::Turkish) {
            QCoreApplication::installTranslator(this);
        }
    }
}

Language AppTranslator::fromCode(const QString& code)
{
    const QString lower = code.trimmed().toLower();
    if (lower == QLatin1String("tr") || lower == QLatin1String("tr_tr")) {
        return Language::Turkish;
    }
    return Language::English;
}

QString AppTranslator::toCode(Language lang)
{
    return lang == Language::Turkish ? QStringLiteral("tr") : QStringLiteral("en");
}

Language AppTranslator::defaultSystemLanguage()
{
    return QLocale::system().language() == QLocale::Turkish ? Language::Turkish
                                                            : Language::English;
}

QString AppTranslator::translate(const char* context,
                                 const char* sourceText,
                                 const char* disambiguation,
                                 int n) const
{
    Q_UNUSED(context)
    Q_UNUSED(disambiguation)
    Q_UNUSED(n)

    if (m_language != Language::Turkish || sourceText == nullptr) {
        return {};
    }

    const auto it = m_trMap.constFind(QByteArray(sourceText));
    if (it != m_trMap.constEnd()) {
        return it.value();
    }
    return {};
}

void AppTranslator::initTurkishDictionary()
{
    auto add = [this](const char* en, const char* tr) {
        m_trMap.insert(QByteArray(en), QString::fromUtf8(tr));
    };

    // General & Window
    add("CCQt Upon", "CCQt Upon");
    add("&File", "&Dosya");
    add("&View", "&Görünüm");
    add("&Language", "&Dil");
    add("English", "English");
    add("Turkish (Türkçe)", "Türkçe");
    add("Connection Settings...", "Bağlantı Ayarları...");
    add("Connection Settings", "Bağlantı Ayarları");
    add("Refresh", "Yenile");
    add("Retry", "Yeniden Dene");
    add("Retry Now", "Şimdi Dene");
    add("Dismiss", "Kapat");
    add("Minimize to Tray on Close", "Kapatırken Tepsiye Küçült");
    add("Show Window", "Pencereyi Göster");
    add("Hide Window", "Pencereyi Gizle");
    add("Quit", "Çıkış");

    // Toolbar & Status
    add("Active Mode:", "Aktif Mod:");
    add("No active mode", "Aktif mod yok");
    add("No modes available", "Kayıtlı mod yok");
    add("Disconnected", "Bağlı değil");
    add("Connecting...", "Bağlanıyor...");
    add("Connected · v%1", "Bağlı · v%1");
    add("Connected", "Bağlı");
    add("Offline — Reconnecting...", "Çevrimdışı — Yeniden bağlanıyor...");
    add("Auth Error (401)", "Kimlik Hatası (401)");
    add("Rate Limited (429)", "İstek Limiti (429)");
    add("URL: %1  |  Polling: 1s  |  Updated: %2", "URL: %1  |  Yoklama: 1 sn  |  Güncelleme: %2");
    add("URL: %1  |  Waiting for daemon...", "URL: %1  |  Servis bekleniyor...");
    add("CCQt Upon — Connected (v%1)", "CCQt Upon — Bağlı (v%1)");
    add("CCQt Upon — Offline", "CCQt Upon — Çevrimdışı");
    add("CCQt Upon — Authentication Required", "CCQt Upon — Kimlik Doğrulama Gerekli");
    add("Active Mode: %1", "Aktif Mod: %1");

    // Sidebar
    add("Modes", "Modlar");
    add("Devices", "Cihazlar");
    add("Profiles", "Profiller");
    add("All Devices (%1)", "Tüm Cihazlar (%1)");
    add("No saved modes found in CoolerControl.", "CoolerControl içinde kayıtlı mod bulunamadı.");
    add("No devices loaded.", "Yüklü cihaz yok.");
    add("No profiles found.", "Profil bulunamadı.");

    // Banners & Errors
    add("coolercontrold is unreachable at %1. Retrying automatically every 2 seconds...",
        "%1 adresindeki coolercontrold servisine ulaşılamıyor. Her 2 saniyede bir yeniden deneniyor...");
    add("Authentication failed (HTTP 401). Please verify your Access Token in Connection Settings.",
        "Kimlik doğrulama başarısız (HTTP 401). Lütfen Bağlantı Ayarları'ndan Access Token'ınızı kontrol edin.");
    add("No Access Token configured. Open Connection Settings to connect to coolercontrold.",
        "Access Token tanımlanmadı. coolercontrold'ye bağlanmak için Bağlantı Ayarları'nı açın.");
    add("Write access denied (HTTP 403). Your Access Token is read-only; enable 'Write Access' to change modes or fan profiles.",
        "Yazma izni reddedildi (HTTP 403). Access Token'ınız salt-okunur; mod veya fan profili değiştirmek için 'Write Access' iznini etkinleştirin.");
    add("Channel is busy or calibrating (HTTP 409): %1",
        "Kanal meşgul veya kalibrasyon sürüyor (HTTP 409): %1");
    add("Rate limited by coolercontrold (HTTP 429). Pausing requests for %1 seconds.",
        "coolercontrold tarafından hız sınırı uygulandı (HTTP 429). İstekler %1 saniye duraklatıldı.");
    add("Failed to apply change: %1", "Değişiklik uygulanamadı: %1");
    add("TLS certificate rejected for non-local host: %1",
        "Yerel olmayan sunucu için TLS sertifikası reddedildi: %1");
    add("Invalid daemon URL. Use http:// or https://.",
        "Geçersiz daemon URL'si. http:// veya https:// kullanın.");

    // Empty / Welcome State
    add("Connect to coolercontrold to view live sensors, temperatures, fan speeds, modes, and profiles.",
        "Canlı sensörleri, sıcaklıkları, fan hızlarını, modları ve profilleri görmek için coolercontrold'ye bağlanın.");
    add("Open Connection Settings", "Bağlantı Ayarlarını Aç");

    // Device Card
    add("Channels & Fans", "Kanallar ve Fanlar");
    add("Temperature Sensors", "Sıcaklık Sensörleri");
    add("Reset", "Sıfırla");
    add("Reset channel to hardware default", "Kanalı donanım varsayılanına sıfırla");
    add("Manual (%1%)", "Manuel (%1%)");
    add("Channel %1 updated", "%1 kanalı güncellendi");
    add("Mode '%1' activated", "'%1' modu etkinleştirildi");
    add("%1 RPM", "%1 RPM");
    add("%1%", "%1%");
    add("%1 °C", "%1 °C");
    add("%1 MHz", "%1 MHz");
    add("%1 W", "%1 W");
    add("Default", "Varsayılan");
    add("Fixed", "Sabit");
    add("Graph", "Grafik");
    add("Mix", "Karışım");
    add("Overlay", "Katman");

    // AuthDialog
    add("CoolerControl Connection Setup", "CoolerControl Bağlantı Kurulumu");
    add("Connection", "Bağlantı");
    add("Daemon URL:", "Daemon URL:");
    add("Self-signed TLS certificates are accepted automatically only for localhost / 127.0.0.1.",
        "Kendinden imzalı TLS sertifikaları yalnızca localhost / 127.0.0.1 için otomatik kabul edilir.");
    add("Access Token:", "Access Token:");
    add("Paste cc_... token here", "cc_... token'ını buraya yapıştırın");
    add("Show", "Göster");
    add("Hide", "Gizle");
    add("Save Access Token locally (0600 permissions)", "Access Token'ı yerel olarak kaydet (0600 izinleriyle)");
    add("Language:", "Dil:");
    add("Test Connection", "Bağlantıyı Test Et");
    add("Testing connection...", "Bağlantı test ediliyor...");
    add("✓ Connected to coolercontrold v%1 (%2)", "✓ coolercontrold v%1 (%2) bağlantısı başarılı");
    add("✗ Authentication failed (HTTP 401): Invalid or expired Access Token.",
        "✗ Kimlik doğrulama başarısız (HTTP 401): Geçersiz veya süresi dolmuş Access Token.");
    add("✗ Cannot reach coolercontrold: %1", "✗ coolercontrold'ye ulaşılamıyor: %1");
    add("Please enter a valid URL.", "Lütfen geçerli bir URL girin.");
    add("Please enter an Access Token.", "Lütfen bir Access Token girin.");
    add("How to Get an Access Token", "Access Token Nasıl Alınır?");
    add(
        "1. Open the official CoolerControl desktop app (or web interface).\n"
        "2. Click \"Access\" in the bottom-left corner.\n"
        "3. Select \"Access Tokens\" from the popup menu.\n"
        "4. Enter a name in the \"Label\" field (e.g. CCQt Upon).\n"
        "5. Leave \"Expiry Date\" empty for a non-expiring token.\n"
        "6. Enable \"Write Access\" so you can switch modes and assign fan profiles.\n"
        "7. Click \"Create Token\", copy the generated cc_... token, and paste it above.",
        "1. Resmi CoolerControl masaüstü uygulamasını (veya web arayüzünü) açın.\n"
        "2. Sol alt köşedeki \"Access\" düğmesine tıklayın.\n"
        "3. Açılan menüden \"Access Tokens\" seçeneğini seçin.\n"
        "4. \"Label\" alanına bir isim girin (ör. CCQt Upon).\n"
        "5. Süresiz kullanım için \"Expiry Date\" bölümünü boş bırakın.\n"
        "6. Mod ve fan profili değiştirebilmek için \"Write Access\" seçeneğini işaretleyin.\n"
        "7. \"Create Token\" düğmesine basın, verilen cc_... token'ını kopyalayıp yukarıdaki alana yapıştırın."
    );
    add("Save & Connect", "Kaydet ve Bağlan");
    add("Cancel", "İptal");
}

} // namespace ccqt
