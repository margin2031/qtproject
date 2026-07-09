#include "serialworker.h"
#include <QDateTime>
#include <QThread>
#include <QTimer>

constexpr double SCALE_COEFF = 2.5124e+05;

SerialWorker::SerialWorker(QObject *parent) : QObject(parent), m_isLogging(false), m_dataFlowEnabled(false)
{
    m_logChannelsMask.fill(1, 7);
    m_serial = new QSerialPort(this);
    m_logFile = nullptr;
    m_logStream = nullptr;

    connect(m_serial, &QSerialPort::readyRead, this, &SerialWorker::readData);

    // QTimer *simTimer = new QTimer(this);
    // connect(simTimer, &QTimer::timeout, this, [this]() {
    //     if (!m_serial->isOpen() && !m_dataFlowEnabled) return;

    //     QString rawLine;
    //     for(int i = 0; i < 7; ++i) {
    //         rawLine += QString::number(rand() % 1000) + "\t";
    //     }
    //     rawLine += QString::number(1000 + rand() % 30) + "\n";
    //     m_buffer.append(rawLine.toUtf8());
    //     readData();
    // });
    // simTimer->start(100);
}

SerialWorker::~SerialWorker()
{
    disconnectFromPort();
    stopLogging();
}

void SerialWorker::setLogChannelsMask(const QVector<int> &mask)
{
    m_logChannelsMask = mask;
}

void SerialWorker::connectToPort(const QString &portName, int baudRate)
{
    if (m_serial->isOpen()) m_serial->close();

    m_serial->setPortName(portName);
    m_serial->setBaudRate(baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (m_serial->open(QIODevice::ReadWrite)) {
        emit connectionStatus(true, "Подключено: " + portName);
    } else {
        emit connectionStatus(false, "Ошибка: " + m_serial->errorString());
    }
}

void SerialWorker::disconnectFromPort()
{
    if (m_serial->isOpen()) {
        m_serial->close();
        emit connectionStatus(false, "Отключено");
    }
}

void SerialWorker::setDataFlowEnabled(bool enabled)
{
    m_dataFlowEnabled = enabled;
    qDebug() << "[SerialWorker] Data flow:" << (enabled ? "ON" : "OFF");
}

bool SerialWorker::startLogging(const QString &filePath, const QString &separator)
{
    if (m_logFile) delete m_logFile;
    m_logFile = new QFile(filePath);
    m_separator = separator;

    if (m_logFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_logStream = new QTextStream(m_logFile);
        *m_logStream << "Timestamp" << m_separator << "Ch1,  мм x 10^4" << m_separator << "Ch2,  мм x 10^4"
                     << m_separator << "Ch3,  мм x 10^4" << m_separator << "Ch4,  мм x 10^4" << m_separator
                     << "Ch5,  мм x 10^4" << m_separator << "Ch6,  мм x 10^4" << m_separator << "Pressure, bar\n";
        m_isLogging = true;
        return true;
    } else {
        emit errorOccurred("Не удалось создать файл логов: " + filePath);
        m_isLogging = false;
        return false;
    }
}

void SerialWorker::stopLogging()
{
    m_isLogging = false;
    if (m_logStream) {
        delete m_logStream;
        m_logStream = nullptr;
    }
    if (m_logFile) {
        m_logFile->close();
        delete m_logFile;
        m_logFile = nullptr;
    }
}

void SerialWorker::readData()
{
    m_buffer.append(m_serial->readAll());

    while (m_buffer.contains('\n')) {
        int pos = m_buffer.indexOf('\n');
        QByteArray line = m_buffer.left(pos).trimmed();
        m_buffer.remove(0, pos + 1);

        QList<QByteArray> parts = line.split('\t');

        for (int i = 0; i < 6 && i < parts.size(); ++i) {
            bool ok = false;
            double value = parts[i].toDouble(&ok);

            if (ok) {
                double mmValue = (value / (2.5124e+05)) * 1e4;
                parts[i] = QByteArray::number(mmValue, 'f', 6);
            }
        }

        if (m_isLogging && m_logStream) {
            QString timeStamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
            *m_logStream << timeStamp;

            for (int i = 0; i < 7; ++i) {
                *m_logStream << m_separator;


                bool shouldLog = (i < m_logChannelsMask.size()) ? (m_logChannelsMask[i] == 1) : true;

                if (shouldLog) {
                    if (i < parts.size()) {
                        *m_logStream << QString(parts[i]);
                    } else {
                        *m_logStream << "0";
                    }
                } else {
                    *m_logStream << "-";
                }
            }
            *m_logStream << "\n";
            m_logStream->flush();
        }

        if (m_dataFlowEnabled) {
            if (parts.size() == 8) {
                QVector<QString> values;
                for (int i = 0; i < 8; i++) {
                    values.append(parts[i]);
                }
                emit dataReceived(values);
            }
        }
    }
}
