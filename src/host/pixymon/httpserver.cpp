#include "httpserver.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QImage>
#include <QRegularExpression>
#include <iostream>

#include "interpreter.h"
#include "renderer.h"

namespace {

// Convert QImage to QByteArray
QByteArray qImageToQByteArray(const QImage &image) {
  QByteArray byteArray;
  QBuffer buffer(&byteArray);
  buffer.open(QIODevice::WriteOnly);

  // Convert QImage to JPEG
  image.save(&buffer, "JPEG");

  return byteArray;
}

}  // namespace

HttpServer::HttpServer() {
  m_interpreter = nullptr;
  m_server = new QHttpServer(this);

  // Setup route for serving snapshots at 'pixy2' with 'action=snapshot'
  m_server->route("/pixy2/", [this](const QHttpServerRequest &request,
                                    QHttpServerResponder &&responder) {
    QUrlQuery query(request.url().query());
    QString action = query.queryItemValue("action");

    if (action == "snapshot") {
      if (m_interpreter && m_interpreter->m_renderer) {
        // Get the image directly from the renderer
        QImage *backgroundImage = m_interpreter->m_renderer->backgroundImage();
        QByteArray byteArray = qImageToQByteArray(*backgroundImage);

        // Send the converted image as a byte array to the client
        responder.write(byteArray, "image/jpeg");
      } else {
        // Respond with an error message if the frame is not available
        responder.write(QByteArray("Snapshot not available"), "text/plain",
                        QHttpServerResponder::StatusCode::NotFound);
      }
    } else if (action == "stream") {
      if (m_interpreter && m_interpreter->m_renderer) {
        // Start streaming
        if (!startStreaming()) {
          responder.write(QByteArray("Failed to start stream"), "text/plain",
                          QHttpServerResponder::StatusCode::InternalServerError);
          return;
        }

        // Keep serving frames while the ffmpeg process is running
        while (m_ffmpeg.isOpen()) {
          QImage *backgroundImage =
              m_interpreter->m_renderer->backgroundImage();

          // Write frame to stdin
          QByteArray frameData = qImageToQByteArray(*backgroundImage);
          m_ffmpeg.write(frameData);
          m_ffmpeg.waitForBytesWritten();

          // Read encoded frame from stdout
          QByteArray encodedFrame = m_ffmpeg.readAllStandardOutput();
          if (!encodedFrame.isEmpty()) {
            responder.write(encodedFrame, "image/jpeg");
          }

          QThread::msleep(16); // Control frame rate
        }
      } else {
        // Respond with an error message if the frame is not available
        responder.write(QByteArray("Stream not available"), "text/plain",
                        QHttpServerResponder::StatusCode::NotFound);
      }
    } else {
      responder.write(QByteArray("Invalid request"), "text/plain",
                      QHttpServerResponder::StatusCode::BadRequest);
    }
  });

  // Start listening
  m_server->listen(QHostAddress::Any, 8080);
}

HttpServer::~HttpServer() {
  // Stop the ffmpeg process
  stopStreaming();

  delete m_server;
  m_server = nullptr;
}

bool HttpServer::startStreaming() {
  // Guard against multiple streams
  if (m_ffmpeg.isOpen()) {
    return false;
  }

  m_ffmpeg.start("ffmpeg", QStringList() << "-f"
                                         << "rawvideo"
                                         << "-pixel_format"
                                         << "rgb24"
                                         << "-video_size"
                                         << "316x208"
                                         << "-i"
                                         << "-"
                                         << "-f"
                                         << "mjpeg"
                                         << "pipe:1");

  return m_ffmpeg.isOpen();
}

void HttpServer::stopStreaming() {
  if (m_ffmpeg.isOpen()) {
    m_ffmpeg.kill();
    m_ffmpeg.waitForFinished();
  }
}
