#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class LspClient : public QObject {
    Q_OBJECT

public:
    struct SpellDiagnostic {
        int startLine = 0;
        int startCharacter = 0;
        int endLine = 0;
        int endCharacter = 0;
        int severity = 3;
        QString code;
        QString message;
        QString language;
        QStringList suggestions;
        QJsonObject rawDiagnostic;

        bool isSpelling() const;
    };

    explicit LspClient(QObject *parent = nullptr);
    ~LspClient() override;

    static QString findLanguageServerExecutable();
    static QByteArray encodeMessage(const QJsonObject &message);
    static QList<QJsonObject> parseMessages(QByteArray &buffer);

    bool isAvailable() const { return m_available; }
    bool isRunning() const;
    bool isInitialized() const { return m_initialized; }

    void start(const QString &programOverride = QString());
    void stop();

    void setConfiguration(const QString &language,
                          const QMap<QString, QStringList> &dictionary);
    void openDocument(const QString &uri, const QString &text);
    void changeDocument(const QString &text);
    void closeDocument();

signals:
    void availableChanged(bool available);
    void diagnosticsReceived(const QString &uri,
                             const QList<LspClient::SpellDiagnostic> &diagnostics);
    void suggestionsUpdated(const QString &uri,
                            const QList<LspClient::SpellDiagnostic> &diagnostics);

private slots:
    void onProcessStarted();
    void onReadyReadStandardOutput();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void handleMessage(const QJsonObject &message);
    void handleServerRequest(const QJsonValue &id, const QString &method,
                             const QJsonObject &params);
    void handleResponse(int id, const QJsonValue &result);
    void handlePublishDiagnostics(const QJsonObject &params);
    void requestCodeActionsForDiagnostics(const QString &uri,
                                          const QList<SpellDiagnostic> &diagnostics);
    void applyCodeActions(const QJsonArray &actions);

    int sendRequest(const QString &method, const QJsonValue &params);
    void sendNotification(const QString &method, const QJsonValue &params);
    void sendResponse(const QJsonValue &id, const QJsonValue &result);
    void sendRawMessage(const QJsonObject &message);
    void sendDidChangeConfiguration();
    void flushPendingDocument();
    QJsonObject buildLtexSettings() const;
    void setAvailable(bool available);

    QProcess m_process;
    QByteArray m_readBuffer;
    bool m_available = false;
    bool m_initialized = false;
    bool m_stopping = false;
    int m_nextRequestId = 1;
    int m_initRequestId = -1;
    int m_codeActionRequestId = -1;
    int m_codeActionVersion = -1;

    QString m_language = QStringLiteral("ru-RU");
    QMap<QString, QStringList> m_dictionary;

    QString m_documentUri;
    QString m_latestText;
    int m_documentVersion = 0;
    bool m_openedOnServer = false;
    bool m_pendingOpen = false;
    bool m_pendingConfigUpdate = false;

    QList<SpellDiagnostic> m_lastDiagnostics;
};
