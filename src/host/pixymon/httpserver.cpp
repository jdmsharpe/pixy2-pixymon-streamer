#include "httpserver.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QRegularExpression>

#include "interpreter.h"
#include "renderer.h"


HttpServer::HttpServer()
{
    m_interpreter = NULL;
    m_server = new QHttpServer(this);
    connect(m_server, SIGNAL(newRequest(QHttpServerRequest*, QHttpServerResponse*)),
            this, SLOT(handleRequest(QHttpServerRequest*, QHttpServerResponse*)));

    m_server->listen(QHostAddress::Any, 8080);
}

HttpServer::~HttpServer()
{
    delete m_server;
}

void HttpServer::newRequest(QHttpServerRequest *req, QHttpServerResponse *resp)
{
    QString reqPath = req->url().path();
    reqPath.remove(QRegularExpression("^[/]*"));

    qDebug() << reqPath;

    // if (reqPath == "frame")
    // {
    //     if (m_interpreter && m_interpreter->m_renderer)
    //     {
    //         QByteArray frame;
    //         m_interpreter->m_renderer->getSavedFrame(&frame);

    //         resp->setHeader("Content-Type", "image/jpeg");  // or the appropriate content type
    //         resp->setHeader("Content-Length", QString::number(frame.size()));
    //         resp->writeHead(QHttpServerResponse::StatusCode::Ok);
    //         resp->end(frame);
    //     }
    //     else
    //     {
    //         // Handle error, e.g., no frame available
    //         resp->writeHead(QHttpServerResponse::StatusCode::NotFound);
    //         resp->end("Frame not available");
    //     }
    // }
    // else
    // {
    //     QDir path = QFileInfo(QCoreApplication::applicationFilePath()).absoluteDir();
    //     QString file = QFileInfo(path, reqPath).absoluteFilePath();

    //     QFile qf(file);

    //     if (!qf.open(QIODevice::ReadOnly))
    //     {
    //         resp->writeHead(QHttpServerResponse::StatusCode::NotFound);
    //         resp->end("File not found");
    //         return;
    //     }

    //     QByteArray fileContent = qf.readAll();
    //     qf.close();

    //     resp->setHeader("Content-Length", QString::number(fileContent.size()));
    //     resp->writeHead(QHttpServerResponse::StatusCode::Ok);
    //     resp->end(fileContent);
    // }
}
