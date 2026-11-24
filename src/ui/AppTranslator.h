#pragma once

#include <QHash>
#include <QString>
#include <QTranslator>

namespace ccqt {

enum class Language {
    English,
    Turkish
};

class AppTranslator : public QTranslator {
    Q_OBJECT

public:
    explicit AppTranslator(QObject* parent = nullptr);

    Language language() const;
    void setLanguage(Language lang);

    static Language fromCode(const QString& code);
    static QString toCode(Language lang);
    static Language defaultLanguage();

    QString translate(const char* context,
                      const char* sourceText,
                      const char* disambiguation = nullptr,
                      int n = -1) const override;

private:
    void initTurkishDictionary();

    Language m_language = Language::English;
    QHash<QByteArray, QString> m_trMap;
};

} // namespace ccqt
