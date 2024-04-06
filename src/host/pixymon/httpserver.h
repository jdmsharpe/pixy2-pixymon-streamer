#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>

class QHttpServer;
class QHttpServerRequest;
class QHttpServerResponse;
class Interpreter;

class HttpServer : public QObject
{
    Q_OBJECT

public:
    HttpServer(Interpreter *interpreter);
    ~HttpServer();

    void setInterpreter(Interpreter *interpreter)
    {
        m_interpreter = interpreter;
    }

private:
    QHttpServer *m_server;
    Interpreter *m_interpreter;
};

#endif // HTTPSERVER_H
