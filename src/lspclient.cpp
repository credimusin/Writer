#include "lspclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStandardPaths>

namespace {

bool rangeMatches(const LspClient::SpellDiagnostic &diag, const QJsonObject &range) {
    const QJsonObject start = range.value(QStringLiteral("start")).toObject();
    const QJsonObject end = range.value(QStringLiteral("end")).toObject();
    return diag.startLine == start.value(QStringLiteral("line")).toInt(-1)
        && diag.startCharacter == start.value(QStringLiteral("character")).toInt(-1)
        && diag.endLine == end.value(QStringLiteral("line")).toInt(-1)
        && diag.endCharacter == end.value(QStringLiteral("character")).toInt(-1);
}

QString extractReplacementText(const QJsonObject &edit, const QString &uri) {
    const QJsonArray docChanges = edit.value(QStringLiteral("documentChanges")).toArray();
    for (const QJsonValue &changeVal : docChanges) {
        const QJsonArray edits = changeVal.toObject().value(QStringLiteral("edits")).toArray();
        if (!edits.isEmpty())
            return edits.first().toObject().value(QStringLiteral("newText")).toString();
    }

    const QJsonObject changes = edit.value(QStringLiteral("changes")).toObject();
    const QJsonArray uriEdits = changes.value(uri).toArray();
    if (!uriEdits.isEmpty())
        return uriEdits.first().toObject().value(QStringLiteral("newText")).toString();

    return {};
}

int parseContentLength(const QByteArray &headerBlock) {
    const QList<QByteArray> lines = headerBlock.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QByteArray name = line.left(colon).trimmed().toLower();
        if (name == "content-length") {
            bool ok = false;
            const int length = line.mid(colon + 1).trimmed().toInt(&ok);
            return (ok && length >= 0) ? length : -1;
        }
    }
    return -1;
}

} // namespace

bool LspClient::SpellDiagnostic::isSpelling() const {
    const QString upper = code.toUpper();
    return upper.contains(QStringLiteral("MORFOLOGIK_RULE"))
        || upper.contains(QStringLiteral("HUNSPELL"))
        || upper.contains(QStringLiteral("SPELLING"))
        || upper.contains(QStringLiteral("TYPO"));
}

LspClient::LspClient(QObject *parent) : QObject(parent) {
    connect(&m_process, &QProcess::started, this, &LspClient::onProcessStarted);
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &LspClient::onReadyReadStandardOutput);
    connect(&m_process, &QProcess::errorOccurred,
            this, &LspClient::onProcessError);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &LspClient::onProcessFinished);
}

LspClient::~LspClient() {
    stop();
}

QString LspClient::findLanguageServerExecutable() {
    static const QStringList candidates = {
        QStringLiteral("ltex-ls"),
        QStringLiteral("ltex-ls-plus")
    };

    for (const QString &name : candidates) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            return found;
    }

    const QStringList extraPaths = {
        QDir::homePath() + QStringLiteral("/.local/bin")
    };
    for (const QString &name : candidates) {
        const QString found = QStandardPaths::findExecutable(name, extraPaths);
        if (!found.isEmpty())
            return found;
    }

    return {};
}

QByteArray LspClient::encodeMessage(const QJsonObject &message) {
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return QByteArrayLiteral("Content-Length: ") + QByteArray::number(payload.size())
        + QByteArrayLiteral("\r\n\r\n") + payload;
}

QList<QJsonObject> LspClient::parseMessages(QByteArray &buffer) {
    QList<QJsonObject> messages;

    while (!buffer.isEmpty()) {
        int headerEnd = buffer.indexOf("\r\n\r\n");
        int separatorLen = 4;
        const int lfHeaderEnd = buffer.indexOf("\n\n");
        if (lfHeaderEnd >= 0 && (headerEnd < 0 || lfHeaderEnd < headerEnd)) {
            headerEnd = lfHeaderEnd;
            separatorLen = 2;
        }
        if (headerEnd < 0)
            break;

        const QByteArray headerBlock = buffer.left(headerEnd);
        const int contentLength = parseContentLength(headerBlock);
        if (contentLength < 0) {
            buffer.remove(0, headerEnd + separatorLen);
            continue;
        }

        const int messageStart = headerEnd + separatorLen;
        if (buffer.size() - messageStart < contentLength)
            break;

        const QByteArray payload = buffer.mid(messageStart, contentLength);
        buffer.remove(0, messageStart + contentLength);

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject())
            messages.append(doc.object());
    }

    return messages;
}

bool LspClient::isRunning() const {
    return m_process.state() != QProcess::NotRunning;
}

void LspClient::start(const QString &programOverride) {
    if (isRunning())
        return;

    const QString program = programOverride.isEmpty()
        ? findLanguageServerExecutable()
        : programOverride;
    if (program.isEmpty()) {
        setAvailable(false);
        return;
    }

    m_stopping = false;
    m_initialized = false;
    m_openedOnServer = false;
    m_initRequestId = -1;
    m_codeActionRequestId = -1;
    m_readBuffer.clear();

    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start(program, QStringList());
}

void LspClient::stop() {
    if (!isRunning()) {
        m_initialized = false;
        m_openedOnServer = false;
        return;
    }

    m_stopping = true;
    if (m_initialized) {
        if (m_openedOnServer && !m_documentUri.isEmpty())
            closeDocument();
        sendRequest(QStringLiteral("shutdown"), QJsonValue::Null);
        sendNotification(QStringLiteral("exit"), QJsonValue::Null);
    }

    m_process.closeWriteChannel();
    if (!m_process.waitForFinished(400)) {
        m_process.terminate();
        if (!m_process.waitForFinished(200)) {
            m_process.kill();
            m_process.waitForFinished(200);
        }
    }

    m_initialized = false;
    m_openedOnServer = false;
    m_readBuffer.clear();
}

void LspClient::setConfiguration(const QString &language,
                                 const QMap<QString, QStringList> &dictionary) {
    const QString normalizedLang = language.trimmed().isEmpty()
        ? QStringLiteral("ru-RU")
        : language.trimmed();
    if (m_language == normalizedLang && m_dictionary == dictionary)
        return;

    m_language = normalizedLang;
    m_dictionary = dictionary;

    if (m_initialized)
        sendDidChangeConfiguration();
    else
        m_pendingConfigUpdate = true;
}

void LspClient::openDocument(const QString &uri, const QString &text) {
    if (uri.isEmpty())
        return;

    if (m_initialized && m_openedOnServer && !m_documentUri.isEmpty() && m_documentUri != uri)
        closeDocument();

    m_documentUri = uri;
    m_latestText = text;
    m_documentVersion = 1;
    m_openedOnServer = false;
    m_pendingOpen = true;
    m_lastDiagnostics.clear();

    if (m_initialized)
        flushPendingDocument();
}

void LspClient::changeDocument(const QString &text) {
    m_latestText = text;
    if (m_documentUri.isEmpty())
        return;

    if (!m_initialized || !m_openedOnServer) {
        m_pendingOpen = true;
        if (m_initialized)
            flushPendingDocument();
        return;
    }

    ++m_documentVersion;
    const QJsonObject params{
        {QStringLiteral("textDocument"), QJsonObject{
            {QStringLiteral("uri"), m_documentUri},
            {QStringLiteral("version"), m_documentVersion}
        }},
        {QStringLiteral("contentChanges"), QJsonArray{
            QJsonObject{{QStringLiteral("text"), m_latestText}}
        }}
    };
    sendNotification(QStringLiteral("textDocument/didChange"), params);
}

void LspClient::closeDocument() {
    if (!m_initialized || !m_openedOnServer || m_documentUri.isEmpty()) {
        m_openedOnServer = false;
        m_pendingOpen = false;
        return;
    }

    sendNotification(QStringLiteral("textDocument/didClose"), QJsonObject{
        {QStringLiteral("textDocument"), QJsonObject{
            {QStringLiteral("uri"), m_documentUri}
        }}
    });
    m_openedOnServer = false;
    m_pendingOpen = false;
    m_lastDiagnostics.clear();
}

void LspClient::onProcessStarted() {
    setAvailable(true);

    const QJsonObject capabilities{
        {QStringLiteral("workspace"), QJsonObject{
            {QStringLiteral("configuration"), true},
            {QStringLiteral("didChangeConfiguration"), QJsonObject{
                {QStringLiteral("dynamicRegistration"), false}
            }}
        }},
        {QStringLiteral("textDocument"), QJsonObject{
            {QStringLiteral("publishDiagnostics"), QJsonObject{
                {QStringLiteral("relatedInformation"), false}
            }},
            {QStringLiteral("codeAction"), QJsonObject{
                {QStringLiteral("codeActionLiteralSupport"), QJsonObject{
                    {QStringLiteral("codeActionKind"), QJsonObject{
                        {QStringLiteral("valueSet"), QJsonArray{
                            QStringLiteral("quickfix.ltex.acceptSuggestions"),
                            QStringLiteral("quickfix.ltex.addToDictionary")
                        }}
                    }}
                }}
            }}
        }}
    };

    const QJsonObject params{
        {QStringLiteral("processId"), qint64(QCoreApplication::applicationPid())},
        {QStringLiteral("rootUri"), QJsonValue::Null},
        {QStringLiteral("capabilities"), capabilities}
    };
    m_initRequestId = sendRequest(QStringLiteral("initialize"), params);
}

void LspClient::onReadyReadStandardOutput() {
    m_readBuffer.append(m_process.readAllStandardOutput());
    const QList<QJsonObject> messages = parseMessages(m_readBuffer);
    for (const QJsonObject &msg : messages)
        handleMessage(msg);
}

void LspClient::onProcessError(QProcess::ProcessError) {
    if (m_stopping)
        return;
    m_initialized = false;
    m_openedOnServer = false;
    setAvailable(false);
}

void LspClient::onProcessFinished(int, QProcess::ExitStatus) {
    m_initialized = false;
    m_openedOnServer = false;
    if (!m_stopping)
        setAvailable(false);
}

void LspClient::handleMessage(const QJsonObject &message) {
    const bool hasId = message.contains(QStringLiteral("id"));
    const bool hasMethod = message.contains(QStringLiteral("method"));

    if (hasId && hasMethod) {
        handleServerRequest(message.value(QStringLiteral("id")),
                            message.value(QStringLiteral("method")).toString(),
                            message.value(QStringLiteral("params")).toObject());
        return;
    }

    if (hasId) {
        handleResponse(message.value(QStringLiteral("id")).toInt(-1),
                       message.value(QStringLiteral("result")));
        return;
    }

    if (hasMethod) {
        const QString method = message.value(QStringLiteral("method")).toString();
        if (method == QLatin1String("textDocument/publishDiagnostics"))
            handlePublishDiagnostics(message.value(QStringLiteral("params")).toObject());
    }
}

void LspClient::handleServerRequest(const QJsonValue &id, const QString &method,
                                    const QJsonObject &params) {
    if (method == QLatin1String("workspace/configuration")) {
        const QJsonArray items = params.value(QStringLiteral("items")).toArray();
        QJsonArray results;
        const QJsonObject ltex = buildLtexSettings();
        for (const QJsonValue &itemVal : items) {
            const QString section = itemVal.toObject().value(QStringLiteral("section")).toString();
            results.append(section == QLatin1String("ltex") ? ltex : QJsonObject{});
        }
        sendResponse(id, results);
        return;
    }

    sendResponse(id, QJsonValue::Null);
}

void LspClient::handleResponse(int id, const QJsonValue &result) {
    if (id == m_initRequestId) {
        m_initRequestId = -1;
        m_initialized = true;
        sendNotification(QStringLiteral("initialized"), QJsonObject{});
        sendDidChangeConfiguration();
        m_pendingConfigUpdate = false;
        flushPendingDocument();
        return;
    }

    if (id == m_codeActionRequestId) {
        m_codeActionRequestId = -1;
        if (m_codeActionVersion == m_documentVersion && result.isArray())
            applyCodeActions(result.toArray());
    }
}

void LspClient::handlePublishDiagnostics(const QJsonObject &params) {
    const QString uri = params.value(QStringLiteral("uri")).toString();
    if (uri != m_documentUri)
        return;

    QList<SpellDiagnostic> parsed;
    const QJsonArray rawList = params.value(QStringLiteral("diagnostics")).toArray();
    parsed.reserve(rawList.size());

    for (const QJsonValue &val : rawList) {
        const QJsonObject obj = val.toObject();
        const QJsonObject range = obj.value(QStringLiteral("range")).toObject();
        const QJsonObject start = range.value(QStringLiteral("start")).toObject();
        const QJsonObject end = range.value(QStringLiteral("end")).toObject();

        SpellDiagnostic diag;
        diag.startLine = start.value(QStringLiteral("line")).toInt(-1);
        diag.startCharacter = start.value(QStringLiteral("character")).toInt(-1);
        diag.endLine = end.value(QStringLiteral("line")).toInt(-1);
        diag.endCharacter = end.value(QStringLiteral("character")).toInt(-1);
        if (diag.startLine < 0 || diag.startCharacter < 0
                || diag.endLine < 0 || diag.endCharacter < 0)
            continue;

        diag.severity = obj.value(QStringLiteral("severity")).toInt(3);
        diag.code = obj.value(QStringLiteral("code")).toString();
        diag.message = obj.value(QStringLiteral("message")).toString();
        diag.language = m_language;
        diag.rawDiagnostic = obj;
        parsed.append(diag);
    }

    m_lastDiagnostics = parsed;
    emit diagnosticsReceived(uri, m_lastDiagnostics);

    if (!m_lastDiagnostics.isEmpty())
        requestCodeActionsForDiagnostics(uri, m_lastDiagnostics);
}

void LspClient::requestCodeActionsForDiagnostics(const QString &uri,
                                                 const QList<SpellDiagnostic> &diagnostics) {
    if (!m_initialized || diagnostics.isEmpty())
        return;

    int minLine = diagnostics.first().startLine;
    int minChar = diagnostics.first().startCharacter;
    int maxLine = diagnostics.first().endLine;
    int maxChar = diagnostics.first().endCharacter;
    QJsonArray rawDiagnostics;

    for (const SpellDiagnostic &diag : diagnostics) {
        if (diag.startLine < minLine || (diag.startLine == minLine && diag.startCharacter < minChar)) {
            minLine = diag.startLine;
            minChar = diag.startCharacter;
        }
        if (diag.endLine > maxLine || (diag.endLine == maxLine && diag.endCharacter > maxChar)) {
            maxLine = diag.endLine;
            maxChar = diag.endCharacter;
        }
        rawDiagnostics.append(diag.rawDiagnostic);
    }

    const QJsonObject params{
        {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uri}}},
        {QStringLiteral("range"), QJsonObject{
            {QStringLiteral("start"), QJsonObject{
                {QStringLiteral("line"), minLine},
                {QStringLiteral("character"), minChar}
            }},
            {QStringLiteral("end"), QJsonObject{
                {QStringLiteral("line"), maxLine},
                {QStringLiteral("character"), maxChar}
            }}
        }},
        {QStringLiteral("context"), QJsonObject{
            {QStringLiteral("diagnostics"), rawDiagnostics}
        }}
    };

    m_codeActionVersion = m_documentVersion;
    m_codeActionRequestId = sendRequest(QStringLiteral("textDocument/codeAction"), params);
}

void LspClient::applyCodeActions(const QJsonArray &actions) {
    if (m_lastDiagnostics.isEmpty())
        return;

    for (const QJsonValue &actionVal : actions) {
        const QJsonObject action = actionVal.toObject();
        const QString kind = action.value(QStringLiteral("kind")).toString();
        const QJsonArray actionDiags = action.value(QStringLiteral("diagnostics")).toArray();

        if (kind == QLatin1String("quickfix.ltex.acceptSuggestions")) {
            const QString replacement = extractReplacementText(
                action.value(QStringLiteral("edit")).toObject(), m_documentUri);
            if (replacement.isEmpty())
                continue;

            QJsonObject targetRange;
            if (!actionDiags.isEmpty()) {
                targetRange = actionDiags.first().toObject().value(QStringLiteral("range")).toObject();
            } else {
                const QJsonArray docChanges = action.value(QStringLiteral("edit"))
                    .toObject().value(QStringLiteral("documentChanges")).toArray();
                if (!docChanges.isEmpty()) {
                    const QJsonArray edits = docChanges.first().toObject()
                        .value(QStringLiteral("edits")).toArray();
                    if (!edits.isEmpty())
                        targetRange = edits.first().toObject().value(QStringLiteral("range")).toObject();
                }
            }
            if (targetRange.isEmpty())
                continue;

            for (SpellDiagnostic &diag : m_lastDiagnostics) {
                if (rangeMatches(diag, targetRange) && !diag.suggestions.contains(replacement))
                    diag.suggestions.append(replacement);
            }
        } else if (kind == QLatin1String("quickfix.ltex.addToDictionary")) {
            const QJsonArray args = action.value(QStringLiteral("command"))
                .toObject().value(QStringLiteral("arguments")).toArray();
            if (args.isEmpty() || actionDiags.isEmpty())
                continue;
            const QJsonObject wordsObj = args.first().toObject().value(QStringLiteral("words")).toObject();
            const QStringList keys = wordsObj.keys();
            if (keys.isEmpty())
                continue;
            const QString dictLang = keys.first();
            const QJsonObject targetRange = actionDiags.first().toObject()
                .value(QStringLiteral("range")).toObject();
            for (SpellDiagnostic &diag : m_lastDiagnostics) {
                if (rangeMatches(diag, targetRange))
                    diag.language = dictLang;
            }
        }
    }

    emit suggestionsUpdated(m_documentUri, m_lastDiagnostics);
}

int LspClient::sendRequest(const QString &method, const QJsonValue &params) {
    const int id = m_nextRequestId++;
    QJsonObject msg{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method}
    };
    if (!params.isUndefined())
        msg.insert(QStringLiteral("params"), params);
    sendRawMessage(msg);
    return id;
}

void LspClient::sendNotification(const QString &method, const QJsonValue &params) {
    QJsonObject msg{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), method}
    };
    if (!params.isUndefined())
        msg.insert(QStringLiteral("params"), params);
    sendRawMessage(msg);
}

void LspClient::sendResponse(const QJsonValue &id, const QJsonValue &result) {
    sendRawMessage(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("result"), result}
    });
}

void LspClient::sendRawMessage(const QJsonObject &message) {
    if (!isRunning())
        return;
    m_process.write(encodeMessage(message));
}

void LspClient::sendDidChangeConfiguration() {
    sendNotification(QStringLiteral("workspace/didChangeConfiguration"), QJsonObject{
        {QStringLiteral("settings"), QJsonObject{
            {QStringLiteral("ltex"), buildLtexSettings()}
        }}
    });
}

void LspClient::flushPendingDocument() {
    if (!m_initialized || !m_pendingOpen || m_documentUri.isEmpty())
        return;

    m_pendingOpen = false;
    m_openedOnServer = true;
    sendNotification(QStringLiteral("textDocument/didOpen"), QJsonObject{
        {QStringLiteral("textDocument"), QJsonObject{
            {QStringLiteral("uri"), m_documentUri},
            {QStringLiteral("languageId"), QStringLiteral("markdown")},
            {QStringLiteral("version"), m_documentVersion},
            {QStringLiteral("text"), m_latestText}
        }}
    });
}

QJsonObject LspClient::buildLtexSettings() const {
    QJsonObject dictObj;
    for (auto it = m_dictionary.cbegin(); it != m_dictionary.cend(); ++it)
        dictObj.insert(it.key(), QJsonArray::fromStringList(it.value()));

    return QJsonObject{
        {QStringLiteral("enabled"), QJsonArray{QStringLiteral("markdown")}},
        {QStringLiteral("language"), m_language},
        {QStringLiteral("dictionary"), dictObj}
    };
}

void LspClient::setAvailable(bool available) {
    if (m_available == available)
        return;
    m_available = available;
    emit availableChanged(m_available);
}
