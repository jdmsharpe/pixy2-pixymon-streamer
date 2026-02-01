#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>
#include <QList>
#include <QTimer>

class Interpreter;
class QTcpServer;
class QTcpSocket;

class HttpServer : public QObject
{
    Q_OBJECT

public:
    HttpServer(quint16 port = 8080);
    ~HttpServer();

    void setInterpreter(Interpreter *interpreter)
    {
        m_interpreter = interpreter;
    }

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();
    void streamFrame();

private:
    void handleRequest(QTcpSocket *client, const QString &request);
    void sendSnapshot(QTcpSocket *client);
    void startMjpegStream(QTcpSocket *client);
    void sendMjpegFrame(QTcpSocket *client);
    QByteArray captureJpegFrame();

    QTcpServer *m_server;
    Interpreter *m_interpreter;
    QList<QTcpSocket*> m_streamClients;
    QTimer m_streamTimer;

    // Cached JPEG frame (encode once, send to all clients)
    QByteArray m_cachedJpeg;

    static const QByteArray MJPEG_BOUNDARY;
};

#endif // HTTPSERVER_H
