#include "httpserver.h"

#include <QBuffer>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QRegularExpression>

#include "interpreter.h"
#include "renderer.h"

namespace {

// Convert Frame8 to QByteArray
QByteArray frame8ToQByteArray(const Frame8& frame) {
    if (!frame.m_pixels || frame.m_width <= 0 || frame.m_height <= 0) {
        return QByteArray();
    }

    // Use QImage::Format_RGB888 for RGB pixel data
    QImage image(frame.m_pixels, frame.m_width, frame.m_height, QImage::Format_RGB888);

    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);

    // Convert QImage to a format like PNG or JPEG
    image.save(&buffer, "PNG");

    return byteArray;
}

} // namespace

HttpServer::HttpServer()
{
    m_interpreter = nullptr;
    m_server = new QHttpServer(this);

    // Setup route for '/pixy2/' with 'action=snapshot'
    m_server->route("/pixy2/", [this](const QHttpServerRequest &request, QHttpServerResponder &&responder) {
        QUrlQuery query(request.url().query());
        QString action = query.queryItemValue("action");

        if (action == "snapshot") {
            if (m_interpreter && m_interpreter->m_renderer) {
                Frame8* rawFrame = m_interpreter->m_renderer->backgroundRaw();
                QByteArray frame = frame8ToQByteArray(*rawFrame);

                // Sending the frame as a response
                responder.write(frame, "image/jpeg"); // or the appropriate content type
            } else {
                // Respond with an error message if the frame is not available
                responder.write(QByteArray("Snapshot not available"), "text/plain", QHttpServerResponder::StatusCode::NotFound);
            }
        } else {
            // Handle other actions or invalid requests
            responder.write(QByteArray("Invalid request"), "text/plain", QHttpServerResponder::StatusCode::BadRequest);
        }
    });

    // Setup route for serving files
    m_server->route("<regex to match file paths>", [this](const QHttpServerRequest &request, QHttpServerResponder &&responder) {
        QString reqPath = request.url().path();
        reqPath.remove(QRegularExpression("^[/]*"));

        QDir path = QFileInfo(QCoreApplication::applicationFilePath()).absoluteDir();
        QString file = QFileInfo(path, reqPath).absoluteFilePath();

        QFile qf(file);
        if (!qf.open(QIODevice::ReadOnly))
        {
            responder.write(QByteArray("File not found"), "text/plain", QHttpServerResponder::StatusCode::NotFound);
            return;
        }

        QByteArray fileContent = qf.readAll();
        qf.close();
        responder.write(fileContent, "application/octet-stream");  // Adjust MIME type based on the file
    });

    // Start listening
    m_server->listen(QHostAddress::Any, 8080);
}

HttpServer::~HttpServer()
{
    delete m_server;
    m_server = nullptr;
}
