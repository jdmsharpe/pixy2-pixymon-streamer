#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QTimer>
#include <QtGlobal>

class Interpreter;
class QImage;
class QTcpServer;
class QTcpSocket;

class HttpServer : public QObject
{
    Q_OBJECT

public:
    HttpServer(quint16 port = 8080);
    ~HttpServer();

    void setInterpreter(Interpreter *interpreter);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();
    void streamFrame();

private:
    void handleRequest(QTcpSocket *client, const QString &method, const QString &path, const QString &version);
    void sendBadRequest(QTcpSocket *client, const QByteArray &message);
    void sendSnapshot(QTcpSocket *client);
    void startMjpegStream(QTcpSocket *client);
    void sendMjpegFrame(QTcpSocket *client);
    QByteArray captureJpegFrame();
    quint64 frameFingerprint(const QImage &frame) const;
    void cacheJpegFrame(const QImage &frame);

    QTcpServer *m_server;
    QPointer<Interpreter> m_interpreter;
    QList<QTcpSocket*> m_streamClients;
    QHash<QTcpSocket*, QByteArray> m_requestBuffers;
    QTimer m_streamTimer;

    // Cached JPEG frame (encode once, send to all clients)
    QByteArray m_cachedJpeg;
    quint64 m_lastFrameFingerprint;
    bool m_hasFrameFingerprint;

    static const QByteArray MJPEG_BOUNDARY;
    static const int MAX_HEADER_SIZE = 16 * 1024; // 16 KB
};

#endif // HTTPSERVER_H
