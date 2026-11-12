#pragma once

#include <QObject>

#if defined(Q_OS_LINUX)
class QDBusVariant;
#endif

class SystemTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString monospaceFamily READ monospaceFamily CONSTANT)

public:
    explicit SystemTheme(QObject *parent = nullptr);

    bool darkMode() const { return m_darkMode; }
    qreal textScale() const { return m_textScale; }
    QString monospaceFamily() const;

signals:
    void darkModeChanged(bool darkMode);
    void textScaleChanged(qreal textScale);

public slots:
    void refresh();

#if defined(Q_OS_LINUX)
private slots:
    void handlePortalSettingChanged(const QString &nameSpace, const QString &key,
                                    const QDBusVariant &value);
#endif

private:
    bool detectDarkMode() const;
    bool qtDarkMode(bool *known) const;
    void setDarkMode(bool darkMode);
    qreal detectTextScale() const;
    void setTextScale(qreal textScale);

    bool m_darkMode = true;
    qreal m_textScale = 1.0;
};
