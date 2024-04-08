#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>

class Interpreter;
class QHttpServer;
class QHttpServerRequest;
class QHttpServerResponse;
class QProcess;

class HttpServer : public QObject
{
    Q_OBJECT

public:
    HttpServer();
    ~HttpServer();

    void setInterpreter(Interpreter *interpreter)
    {
        m_interpreter = interpreter;
    }

private:
    bool startStreaming();

    void stopStreaming();

    QHttpServer *m_server;
    Interpreter *m_interpreter;
    QProcess m_ffmpeg;
};

#endif // HTTPSERVER_H
