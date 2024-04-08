#include "httpserver.h"

#include <iostream>
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

// Convert QImage to QByteArray
QByteArray qImageToQByteArray(const QImage& image) {
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    buffer.open(QIODevice::WriteOnly);

    // Convert QImage to PNG - looks better for Pixy than jpeg due to compression artifacts
    image.save(&buffer, "PNG");

    return byteArray;
}

} // namespace

HttpServer::HttpServer()
{
    m_interpreter = nullptr;
    m_server = new QHttpServer(this);

    // Setup route for serving snapshots at '' with 'action=snapshot'
    m_server->route("", [this](const QHttpServerRequest &request, QHttpServerResponder &&responder) {
        QUrlQuery query(request.url().query());
        QString action = query.queryItemValue("action");

        if (action == "snapshot") {
            if (m_interpreter && m_interpreter->m_renderer) {
                // Get the image directly from the renderer
                QImage* backgroundImage = m_interpreter->m_renderer->backgroundImage();
                QByteArray byteArray = qImageToQByteArray(*backgroundImage);

                // Send the converted image as a byte array to the client
                responder.write(byteArray, "image/png");
            } else {
                // Respond with an error message if the frame is not available
                responder.write(QByteArray("Snapshot not available"), "text/plain", QHttpServerResponder::StatusCode::NotFound);
            }
        } else {
            // Handle other actions or invalid requests
            responder.write(QByteArray("Invalid request"), "text/plain", QHttpServerResponder::StatusCode::BadRequest);
        }
    });

    // Setup route for serving streams at '/webcam/' with 'action=stream'
    m_server->route("/webcam/", [this](const QHttpServerRequest &request, QHttpServerResponder &&responder) {
        QUrlQuery query(request.url().query());
        QString action = query.queryItemValue("action");

        if (action == "stream") {
            // Start streaming MJPEG frames
            // You'll need to implement the logic to continuously capture frames
            // and use FFmpeg to encode them as MJPEG, sending each frame in response.
        } else {
            responder.write(QByteArray("Invalid request"), "text/plain", QHttpServerResponder::StatusCode::BadRequest);
        }
    });

    // Start listening
    m_server->listen(QHostAddress::Any, 8080);
}

HttpServer::~HttpServer()
{
    delete m_server;
    m_server = nullptr;
}
