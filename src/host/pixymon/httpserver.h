#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtGlobal>

class Interpreter;
class QImage;
class QTcpServer;
class QTcpSocket;

class HttpServer : public QObject {
  Q_OBJECT

 public:
  struct Options {
    QHostAddress bindAddress;
    quint16 port;
    int fps;
    int jpegQuality;
    int maxClients;
    int requestTimeoutMs;
  };

  explicit HttpServer(const Options& options, QObject* parent = nullptr);
  ~HttpServer();

  void setInterpreter(Interpreter* interpreter);

 private slots:
  void onNewConnection();
  void onClientReadyRead();
  void onClientDisconnected();
  void streamFrame();

 private:
  void handleRequest(QTcpSocket* client, const QString& method,
                     const QString& path, const QString& version);
  void sendBadRequest(QTcpSocket* client, const QByteArray& message);
  void sendTextResponse(QTcpSocket* client, int statusCode,
                        const QByteArray& reason, const QByteArray& body,
                        const QByteArray& contentType = "text/plain");
  void sendJsonResponse(QTcpSocket* client, const QByteArray& jsonData);
  void sendSnapshot(QTcpSocket* client);
  void sendStatus(QTcpSocket* client);
  void startMjpegStream(QTcpSocket* client);
  void sendMjpegFrame(QTcpSocket* client);
  void startRequestTimer(QTcpSocket* client);
  void stopRequestTimer(QTcpSocket* client);
  QByteArray captureJpegFrame();
  quint64 frameFingerprint(const QImage& frame) const;
  void cacheJpegFrame(const QImage& frame);

  QTcpServer* m_server;
  QPointer<Interpreter> m_interpreter;
  QList<QTcpSocket*> m_clients;
  QList<QTcpSocket*> m_streamClients;
  QHash<QTcpSocket*, QByteArray> m_requestBuffers;
  QHash<QTcpSocket*, QTimer*> m_requestTimers;
  QTimer m_streamTimer;

  QHostAddress m_bindAddress;
  quint16 m_port;
  int m_fps;
  int m_frameIntervalMs;
  int m_jpegQuality;
  int m_maxClients;
  int m_requestTimeoutMs;

  // Cached JPEG frame (encode once, send to all clients)
  QByteArray m_cachedJpeg;
  quint64 m_lastFrameFingerprint;
  bool m_hasFrameFingerprint;

  static const QByteArray MJPEG_BOUNDARY;
  static const int MAX_HEADER_SIZE = 16 * 1024;  // 16 KB
};

#endif  // HTTPSERVER_H
