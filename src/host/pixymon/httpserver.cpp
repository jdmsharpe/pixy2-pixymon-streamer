#include "httpserver.h"

#include <QBuffer>
#include <QImage>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QUrl>

#include "interpreter.h"
#include "renderer.h"

const QByteArray HttpServer::MJPEG_BOUNDARY = "----pixyframe";

HttpServer::HttpServer(quint16 port)
{
    m_interpreter = nullptr;
    m_lastFramePtr = nullptr;
    m_server = new QTcpServer(this);

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

    // Read the HTTP request
    QByteArray requestData = client->readAll();
    QString request = QString::fromUtf8(requestData);

    // Parse first line to get the request path
    QStringList lines = request.split("\r\n");
    if (lines.isEmpty()) return;

    QStringList requestLine = lines.first().split(" ");
    if (requestLine.size() < 2) return;

    QString path = requestLine[1];
    handleRequest(client, path);
}

void HttpServer::onClientDisconnected()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    // Remove from stream clients if present
    m_streamClients.removeAll(client);

    // Stop timer if no more stream clients
    if (m_streamClients.isEmpty()) {
        m_streamTimer.stop();
    }

    client->deleteLater();
}

void HttpServer::handleRequest(QTcpSocket *client, const QString &path)
{
    QUrl url(path);
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

QByteArray HttpServer::captureJpegFrame()
{
    if (!m_interpreter || !m_interpreter->m_renderer) {
        return QByteArray();
    }

    QImage *image = m_interpreter->m_renderer->backgroundImage();
    if (!image || image->isNull()) {
        return QByteArray();
    }

    QByteArray jpegData;
    QBuffer buffer(&jpegData);
    buffer.open(QIODevice::WriteOnly);
    image->save(&buffer, "JPEG", 85);

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

    // Add to stream clients
    m_streamClients.append(client);

    // Capture first frame immediately if we don't have one cached
    QImage *currentFrame = m_interpreter->m_renderer->backgroundImage();
    if (currentFrame && !currentFrame->isNull() &&
        (m_cachedJpeg.isEmpty() || currentFrame != m_lastFramePtr)) {
        m_lastFramePtr = currentFrame;
        m_cachedJpeg.clear();
        QBuffer buffer(&m_cachedJpeg);
        buffer.open(QIODevice::WriteOnly);
        currentFrame->save(&buffer, "JPEG", 85);
    }

    // Send first frame immediately
    sendMjpegFrame(client);

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
    // Check if we have a new frame to send
    if (!m_interpreter || !m_interpreter->m_renderer) {
        return;
    }

    QImage *currentFrame = m_interpreter->m_renderer->backgroundImage();

    // Only encode if frame pointer changed (new frame from Pixy)
    if (currentFrame != m_lastFramePtr && currentFrame && !currentFrame->isNull()) {
        m_lastFramePtr = currentFrame;

        // Encode new frame to JPEG
        m_cachedJpeg.clear();
        QBuffer buffer(&m_cachedJpeg);
        buffer.open(QIODevice::WriteOnly);
        currentFrame->save(&buffer, "JPEG", 85);
    }

    // If no cached frame, nothing to send
    if (m_cachedJpeg.isEmpty()) {
        return;
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
