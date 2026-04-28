#include "httpserver.h"

#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include "interpreter.h"
#include "renderer.h"

const QByteArray HttpServer::MJPEG_BOUNDARY = "----pixyframe";

HttpServer::HttpServer(const Options& options, QObject* parent)
    : QObject(parent),
      m_bindAddress(options.bindAddress),
      m_port(options.port),
      m_fps(qMax(1, options.fps)),
      m_frameIntervalMs(qMax(1, 1000 / qMax(1, options.fps))),
      m_jpegQuality(options.jpegQuality),
      m_maxClients(qMax(1, options.maxClients)),
      m_requestTimeoutMs(qMax(1000, options.requestTimeoutMs)) {
  m_interpreter = nullptr;
  m_server = new QTcpServer(this);
  m_lastFrameFingerprint = 0;
  m_hasFrameFingerprint = false;

  connect(m_server, &QTcpServer::newConnection, this,
          &HttpServer::onNewConnection);

  // Stream timer sends frames to all connected stream clients
  connect(&m_streamTimer, &QTimer::timeout, this, &HttpServer::streamFrame);

  if (m_server->listen(m_bindAddress, m_port)) {
    qDebug() << "HTTP server listening on" << m_bindAddress.toString() << "port"
             << m_port;
  } else {
    qWarning() << "Failed to start HTTP server:" << m_server->errorString();
  }
}

HttpServer::~HttpServer() {
  m_streamTimer.stop();

  QList<QTcpSocket*> clients = m_clients;
  for (QTcpSocket* client : clients) {
    stopRequestTimer(client);
    client->disconnect(this);
    client->close();
    client->deleteLater();
  }
  m_clients.clear();
  m_streamClients.clear();
  m_requestBuffers.clear();

  m_server->close();
}

void HttpServer::setInterpreter(Interpreter* interpreter) {
  m_interpreter = interpreter;

  if (!m_interpreter) {
    m_streamTimer.stop();

    for (QTcpSocket* client : m_streamClients) {
      client->close();
    }
    m_streamClients.clear();
    m_cachedJpeg.clear();
    m_hasFrameFingerprint = false;
  }
}

void HttpServer::onNewConnection() {
  while (m_server->hasPendingConnections()) {
    QTcpSocket* client = m_server->nextPendingConnection();

    if (m_clients.size() >= m_maxClients) {
      sendTextResponse(client, 503, "Service Unavailable",
                       "Too many HTTP clients connected.\n");
      client->disconnectFromHost();
      client->deleteLater();
      continue;
    }

    client->setParent(this);
    m_clients.append(client);
    m_requestBuffers.insert(client, QByteArray());

    connect(client, &QTcpSocket::readyRead, this,
            &HttpServer::onClientReadyRead);
    connect(client, &QTcpSocket::disconnected, this,
            &HttpServer::onClientDisconnected);

    startRequestTimer(client);
  }
}

void HttpServer::onClientReadyRead() {
  QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
  if (!client) return;

  // Ignore read activity from established stream clients.
  if (m_streamClients.contains(client)) {
    client->readAll();
    return;
  }

  QByteArray& buffer = m_requestBuffers[client];
  buffer.append(client->readAll());

  if (buffer.size() > MAX_HEADER_SIZE) {
    sendBadRequest(client, "Request header too large");
    m_requestBuffers.remove(client);
    stopRequestTimer(client);
    client->disconnectFromHost();
    return;
  }

  const int headerEnd = buffer.indexOf("\r\n\r\n");
  if (headerEnd == -1) {
    // Wait for complete headers.
    return;
  }

  QByteArray headerData = buffer.left(headerEnd);
  m_requestBuffers.remove(client);
  stopRequestTimer(client);

  const int firstLineEnd = headerData.indexOf("\r\n");
  QByteArray requestLineBytes =
      firstLineEnd == -1 ? headerData : headerData.left(firstLineEnd);
  QString requestLine = QString::fromLatin1(requestLineBytes).trimmed();

  QStringList requestLineParts = requestLine.split(' ', Qt::SkipEmptyParts);
  if (requestLineParts.size() != 3) {
    sendBadRequest(client, "Malformed request line");
    client->disconnectFromHost();
    return;
  }

  const QString method = requestLineParts[0];
  const QString path = requestLineParts[1];
  const QString version = requestLineParts[2];

  if (method != "GET") {
    sendBadRequest(client, "Only GET is supported");
    client->disconnectFromHost();
    return;
  }

  if (!(version == "HTTP/1.1" || version == "HTTP/1.0")) {
    sendBadRequest(client, "Unsupported HTTP version");
    client->disconnectFromHost();
    return;
  }

  handleRequest(client, method, path, version);
}

void HttpServer::onClientDisconnected() {
  QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
  if (!client) return;

  // Remove from stream clients if present
  m_clients.removeAll(client);
  m_streamClients.removeAll(client);
  m_requestBuffers.remove(client);
  stopRequestTimer(client);

  // Stop timer if no more stream clients
  if (m_streamClients.isEmpty()) {
    m_streamTimer.stop();
  }

  client->deleteLater();
}

void HttpServer::handleRequest(QTcpSocket* client, const QString&,
                               const QString& path, const QString&) {
  QUrl url(path);
  if (!url.isValid()) {
    sendBadRequest(client, "Malformed URL");
    client->disconnectFromHost();
    return;
  }

  QUrlQuery query(url.query());
  QString action = query.queryItemValue("action");

  const QString urlPath = url.path();

  // Check if this is a request to /pixy2/
  if (!(urlPath == "/pixy2" || urlPath.startsWith("/pixy2/"))) {
    // Send 404 for unknown paths
    sendTextResponse(client, 404, "Not Found",
                     "Not Found. Use /pixy2/?action=snapshot, "
                     "/pixy2/?action=stream, or /pixy2/?action=status\n");
    client->disconnectFromHost();
    return;
  }

  if (action == "snapshot") {
    sendSnapshot(client);
  } else if (action == "stream") {
    startMjpegStream(client);
  } else if (action == "status") {
    sendStatus(client);
  } else {
    // Default: show usage info
    QByteArray body =
        "Pixy2 HTTP Stream Server\n"
        "========================\n"
        "Endpoints:\n"
        "  /pixy2/?action=snapshot  - Get single JPEG frame\n"
        "  /pixy2/?action=stream    - MJPEG stream (for OctoPrint)\n"
        "  /pixy2/?action=status    - JSON server and camera status\n";

    sendTextResponse(client, 200, "OK", body);
    client->disconnectFromHost();
  }
}

void HttpServer::sendBadRequest(QTcpSocket* client, const QByteArray& message) {
  QByteArray body = "Bad Request: " + message + "\n";
  sendTextResponse(client, 400, "Bad Request", body);
}

void HttpServer::sendTextResponse(QTcpSocket* client, int statusCode,
                                  const QByteArray& reason,
                                  const QByteArray& body,
                                  const QByteArray& contentType) {
  QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " +
                        reason +
                        "\r\n"
                        "Content-Type: " +
                        contentType +
                        "\r\n"
                        "Content-Length: " +
                        QByteArray::number(body.size()) +
                        "\r\n"
                        "Connection: close\r\n"
                        "\r\n" +
                        body;

  client->write(response);
  client->flush();
}

void HttpServer::sendJsonResponse(QTcpSocket* client,
                                  const QByteArray& jsonData) {
  sendTextResponse(client, 200, "OK", jsonData, "application/json");
}

QByteArray HttpServer::captureJpegFrame() {
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
  image.save(&buffer, "JPEG", m_jpegQuality);

  return jpegData;
}

void HttpServer::sendSnapshot(QTcpSocket* client) {
  QByteArray jpegData = captureJpegFrame();

  if (jpegData.isEmpty()) {
    sendTextResponse(client, 503, "Service Unavailable",
                     "No frame available. Is Pixy connected?\n");
    client->disconnectFromHost();
    return;
  }

  QByteArray response =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: image/jpeg\r\n"
      "Content-Length: " +
      QByteArray::number(jpegData.size()) +
      "\r\n"
      "Cache-Control: no-cache\r\n"
      "Connection: close\r\n"
      "\r\n";

  client->write(response);
  client->write(jpegData);
  client->flush();
  client->disconnectFromHost();
}

void HttpServer::sendStatus(QTcpSocket* client) {
  QImage frame;
  if (m_interpreter && m_interpreter->m_renderer) {
    frame = m_interpreter->m_renderer->backgroundImage();
  }

  QJsonObject server;
  server["listening"] = m_server->isListening();
  server["bindAddress"] = m_bindAddress.toString();
  server["port"] = static_cast<int>(m_port);
  server["fps"] = m_fps;
  server["jpegQuality"] = m_jpegQuality;
  server["maxClients"] = m_maxClients;
  server["requestTimeoutMs"] = m_requestTimeoutMs;
  server["clients"] = static_cast<int>(m_clients.size());
  server["streamClients"] = static_cast<int>(m_streamClients.size());
  server["cachedJpegBytes"] = static_cast<int>(m_cachedJpeg.size());

  QJsonObject camera;
  camera["pixyConnected"] = !m_interpreter.isNull();
  camera["rendererAvailable"] = m_interpreter && m_interpreter->m_renderer;
  camera["frameAvailable"] = !frame.isNull();
  camera["frameWidth"] = frame.isNull() ? 0 : frame.width();
  camera["frameHeight"] = frame.isNull() ? 0 : frame.height();

  QJsonObject status;
  status["server"] = server;
  status["camera"] = camera;

  sendJsonResponse(client,
                   QJsonDocument(status).toJson(QJsonDocument::Compact) + "\n");
  client->disconnectFromHost();
}

void HttpServer::startMjpegStream(QTcpSocket* client) {
  if (!m_interpreter || !m_interpreter->m_renderer) {
    sendTextResponse(client, 503, "Service Unavailable",
                     "Stream not available. Is Pixy connected?\n");
    client->disconnectFromHost();
    return;
  }

  // Send MJPEG stream headers
  QByteArray response =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: multipart/x-mixed-replace; boundary=" +
      MJPEG_BOUNDARY +
      "\r\n"
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
    m_lastFrameFingerprint = frameFingerprint(currentFrame);
    m_hasFrameFingerprint = true;
    sendMjpegFrame(client);
  }

  // Start timer if not already running.
  if (!m_streamTimer.isActive()) {
    m_streamTimer.start(m_frameIntervalMs);
  }
}

void HttpServer::sendMjpegFrame(QTcpSocket* client) {
  if (!client->isOpen() || m_cachedJpeg.isEmpty()) return;

  // MJPEG frame format using cached JPEG
  QByteArray frame = "--" + MJPEG_BOUNDARY +
                     "\r\n"
                     "Content-Type: image/jpeg\r\n"
                     "Content-Length: " +
                     QByteArray::number(m_cachedJpeg.size()) +
                     "\r\n"
                     "\r\n";

  client->write(frame);
  client->write(m_cachedJpeg);
  client->write("\r\n");
  client->flush();
}

void HttpServer::streamFrame() {
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

  for (QTcpSocket* client : m_streamClients) {
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
  for (QTcpSocket* client : disconnected) {
    m_clients.removeAll(client);
    m_streamClients.removeAll(client);
    m_requestBuffers.remove(client);
    stopRequestTimer(client);
    client->deleteLater();
  }

  // Stop timer if no clients left
  if (m_streamClients.isEmpty()) {
    m_streamTimer.stop();
  }
}

quint64 HttpServer::frameFingerprint(const QImage& frame) const {
  if (frame.isNull()) {
    return 0;
  }

  const uchar* bits = frame.constBits();
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

void HttpServer::cacheJpegFrame(const QImage& frame) {
  m_cachedJpeg.clear();
  QBuffer buffer(&m_cachedJpeg);
  buffer.open(QIODevice::WriteOnly);
  frame.save(&buffer, "JPEG", m_jpegQuality);
}

void HttpServer::startRequestTimer(QTcpSocket* client) {
  if (m_requestTimeoutMs <= 0) {
    return;
  }

  QTimer* timer = new QTimer(client);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [this, client]() {
    if (!m_clients.contains(client) || m_streamClients.contains(client)) {
      return;
    }

    sendTextResponse(client, 408, "Request Timeout",
                     "Request timed out waiting for complete headers.\n");
    m_requestBuffers.remove(client);
    client->disconnectFromHost();
  });

  m_requestTimers.insert(client, timer);
  timer->start(m_requestTimeoutMs);
}

void HttpServer::stopRequestTimer(QTcpSocket* client) {
  QTimer* timer = m_requestTimers.take(client);
  if (!timer) {
    return;
  }

  timer->stop();
  timer->deleteLater();
}
