#include "httpserver.h"

#include <QBuffer>
#include <QImage>
#include <QTcpServer>
#include <QTcpSocket>
#include <QStringList>
#include <QUrlQuery>
#include <QUrl>

#include "interpreter.h"
#include "renderer.h"

const QByteArray HttpServer::MJPEG_BOUNDARY = "----pixyframe";

HttpServer::HttpServer(quint16 port)
{
    m_interpreter = nullptr;
    m_server = new QTcpServer(this);
    m_lastFrameFingerprint = 0;
    m_hasFrameFingerprint = false;

    connect(m_server, &QTcpServer::newConnection,
            this, &HttpServer::onNewConnection);

    // Stream timer sends frames to all connected stream clients
    connect(&m_streamTimer, &QTimer::timeout,
            this, &HttpServer::streamFrame);

    if (m_server->listen(QHostAddress::Any, port)) {
        qDebug() << "HTTP server listening on port" << port;
    } else {
        qWarning() << "Failed to start HTTP server:" << m_server->errorString();
    }
}

HttpServer::~HttpServer()
{
    m_streamTimer.stop();

    // Close all streaming clients
    for (QTcpSocket *client : m_streamClients) {
        client->close();
    }
    m_streamClients.clear();

    m_server->close();
}

void HttpServer::setInterpreter(Interpreter *interpreter)
{
    m_interpreter = interpreter;

    if (!m_interpreter) {
        m_streamTimer.stop();

        for (QTcpSocket *client : m_streamClients) {
            client->close();
        }
        m_streamClients.clear();
        m_cachedJpeg.clear();
    }
}

void HttpServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *client = m_server->nextPendingConnection();

        connect(client, &QTcpSocket::readyRead,
                this, &HttpServer::onClientReadyRead);
        connect(client, &QTcpSocket::disconnected,
                this, &HttpServer::onClientDisconnected);
    }
}

void HttpServer::onClientReadyRead()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    // Ignore read activity from established stream clients.
    if (m_streamClients.contains(client)) {
        client->readAll();
        return;
    }

    QByteArray &buffer = m_requestBuffers[client];
    buffer.append(client->readAll());

    if (buffer.size() > MAX_HEADER_SIZE) {
        sendBadRequest(client, "Request header too large");
        m_requestBuffers.remove(client);
        client->close();
        return;
    }

    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd == -1) {
        // Wait for complete headers.
        return;
    }

    QByteArray headerData = buffer.left(headerEnd);
    m_requestBuffers.remove(client);

    const int firstLineEnd = headerData.indexOf("\r\n");
    QByteArray requestLineBytes = firstLineEnd == -1 ? headerData : headerData.left(firstLineEnd);
    QString requestLine = QString::fromLatin1(requestLineBytes).trimmed();

    QStringList requestLineParts = requestLine.split(' ', Qt::SkipEmptyParts);
    if (requestLineParts.size() != 3) {
        sendBadRequest(client, "Malformed request line");
        client->close();
        return;
    }

    const QString method = requestLineParts[0];
    const QString path = requestLineParts[1];
    const QString version = requestLineParts[2];

    if (method != "GET") {
        sendBadRequest(client, "Only GET is supported");
        client->close();
        return;
    }

    if (!(version == "HTTP/1.1" || version == "HTTP/1.0")) {
        sendBadRequest(client, "Unsupported HTTP version");
        client->close();
        return;
    }

    handleRequest(client, method, path, version);
}

void HttpServer::onClientDisconnected()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    // Remove from stream clients if present
    m_streamClients.removeAll(client);
    m_requestBuffers.remove(client);

    // Stop timer if no more stream clients
    if (m_streamClients.isEmpty()) {
        m_streamTimer.stop();
    }

    client->deleteLater();
}

void HttpServer::handleRequest(QTcpSocket *client, const QString &, const QString &path, const QString &)
{
    QUrl url(path);
    if (!url.isValid()) {
        sendBadRequest(client, "Malformed URL");
        client->close();
        return;
    }

    QUrlQuery query(url.query());
    QString action = query.queryItemValue("action");

    // Check if this is a request to /pixy2/
    if (!url.path().startsWith("/pixy2")) {
        // Send 404 for unknown paths
        QByteArray response =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Not Found. Use /pixy2/?action=snapshot or /pixy2/?action=stream";
        client->write(response);
        client->flush();
        client->close();
        return;
    }

    if (action == "snapshot") {
        sendSnapshot(client);
    } else if (action == "stream") {
        startMjpegStream(client);
    } else {
        // Default: show usage info
        QByteArray body =
            "Pixy2 HTTP Stream Server\n"
            "========================\n"
            "Endpoints:\n"
            "  /pixy2/?action=snapshot  - Get single JPEG frame\n"
            "  /pixy2/?action=stream    - MJPEG stream (for OctoPrint)\n";

        QByteArray response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        client->write(response);
        client->flush();
        client->close();
    }
}

void HttpServer::sendBadRequest(QTcpSocket *client, const QByteArray &message)
{
    QByteArray body = "Bad Request: " + message + "\n";
    QByteArray response =
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    client->write(response);
    client->flush();
}

QByteArray HttpServer::captureJpegFrame()
{
    if (!m_interpreter || !m_interpreter->m_renderer) {
        return QByteArray();
    }

    QImage image = m_interpreter->m_renderer->backgroundImage();
    if (image.isNull()) {
        return QByteArray();
    }

    QByteArray jpegData;
    QBuffer buffer(&jpegData);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 85);

    return jpegData;
}

void HttpServer::sendSnapshot(QTcpSocket *client)
{
    QByteArray jpegData = captureJpegFrame();

    if (jpegData.isEmpty()) {
        QByteArray response =
            "HTTP/1.1 503 Service Unavailable\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n"
            "\r\n"
            "No frame available. Is Pixy connected?";
        client->write(response);
        client->flush();
        client->close();
        return;
    }

    QByteArray response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: " + QByteArray::number(jpegData.size()) + "\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n"
        "\r\n";

    client->write(response);
    client->write(jpegData);
    client->flush();
    client->close();
}

void HttpServer::startMjpegStream(QTcpSocket *client)
{
    if (!m_interpreter || !m_interpreter->m_renderer) {
        QByteArray response =
            "HTTP/1.1 503 Service Unavailable\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Stream not available. Is Pixy connected?";
        client->write(response);
        client->flush();
        client->close();
        return;
    }

    // Send MJPEG stream headers
    QByteArray response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=" + MJPEG_BOUNDARY + "\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    client->write(response);
    client->flush();

    m_requestBuffers.remove(client);

    // Add to stream clients
    if (!m_streamClients.contains(client)) {
        m_streamClients.append(client);
    }

    // Capture and send first frame immediately
    QImage currentFrame = m_interpreter->m_renderer->backgroundImage();
    if (!currentFrame.isNull()) {
        cacheJpegFrame(currentFrame);
        sendMjpegFrame(client);
    }

    // Start timer if not already running (60 FPS)
    if (!m_streamTimer.isActive()) {
        m_streamTimer.start(16);  // ~60 FPS (Pixy supports up to 61)
    }
}

void HttpServer::sendMjpegFrame(QTcpSocket *client)
{
    if (!client->isOpen() || m_cachedJpeg.isEmpty()) return;

    // MJPEG frame format using cached JPEG
    QByteArray frame =
        "--" + MJPEG_BOUNDARY + "\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: " + QByteArray::number(m_cachedJpeg.size()) + "\r\n"
        "\r\n";

    client->write(frame);
    client->write(m_cachedJpeg);
    client->write("\r\n");
    client->flush();
}

void HttpServer::streamFrame()
{
    // Check if we have a renderer
    if (!m_interpreter || !m_interpreter->m_renderer) {
        if (m_streamTimer.isActive()) {
            m_streamTimer.stop();
        }
        return;
    }

    QImage currentFrame = m_interpreter->m_renderer->backgroundImage();
    if (currentFrame.isNull()) {
        return;
    }

    // Encode only when frame content changed, otherwise keep cached JPEG
    quint64 currentFingerprint = frameFingerprint(currentFrame);
    if (!m_hasFrameFingerprint || currentFingerprint != m_lastFrameFingerprint) {
        cacheJpegFrame(currentFrame);
        m_lastFrameFingerprint = currentFingerprint;
        m_hasFrameFingerprint = true;
    }

    // Send cached frame to all connected stream clients
    QList<QTcpSocket*> disconnected;

    for (QTcpSocket *client : m_streamClients) {
        if (client->state() == QAbstractSocket::ConnectedState) {
            // Check socket buffer - skip if backing up
            if (client->bytesToWrite() < 100000) {
                sendMjpegFrame(client);
            }
        } else {
            disconnected.append(client);
        }
    }

    // Clean up disconnected clients
    for (QTcpSocket *client : disconnected) {
        m_streamClients.removeAll(client);
        client->deleteLater();
    }

    // Stop timer if no clients left
    if (m_streamClients.isEmpty()) {
        m_streamTimer.stop();
    }
}

quint64 HttpServer::frameFingerprint(const QImage &frame) const
{
    if (frame.isNull()) {
        return 0;
    }

    const uchar *bits = frame.constBits();
    const qsizetype size = frame.sizeInBytes();

    // 64-bit FNV-1a over raw frame bytes
    quint64 hash = 1469598103934665603ULL;
    for (qsizetype i = 0; i < size; ++i) {
        hash ^= static_cast<quint64>(bits[i]);
        hash *= 1099511628211ULL;
    }

    hash ^= static_cast<quint64>(frame.width());
    hash *= 1099511628211ULL;
    hash ^= static_cast<quint64>(frame.height());
    hash *= 1099511628211ULL;
    hash ^= static_cast<quint64>(frame.format());

    return hash;
}

void HttpServer::cacheJpegFrame(const QImage &frame)
{
    m_cachedJpeg.clear();
    QBuffer buffer(&m_cachedJpeg);
    buffer.open(QIODevice::WriteOnly);
    frame.save(&buffer, "JPEG", 85);
}
