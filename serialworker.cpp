#include "serialworker.h"
#include <QDebug>
#include <cmath>
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
    m_buffer.clear();
    m_dataFlowEnabled = false;

    m_serial->setPortName(portName);
#ifdef Q_OS_WIN
    // Отдельно проверяем открытие порта и установку выбранной скорости.
    m_serial->setBaudRate(QSerialPort::Baud9600);
#else
    m_serial->setBaudRate(baudRate);
#endif
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    const auto portError = [this, &portName](const QString &stage) {
        return QString("%1: %2. Ошибка Qt %3: %4 (Qt %5)")
            .arg(portName, stage)
            .arg(static_cast<int>(m_serial->error()))
            .arg(m_serial->errorString(), QString::fromLatin1(qVersion()));
    };

    if (!m_serial->open(QIODevice::ReadWrite)) {
        emit connectionStatus(false, portError("Не удалось открыть порт"));
        return;
    }

#ifdef Q_OS_WIN
    if (!m_serial->setBaudRate(baudRate, QSerialPort::AllDirections)) {
        const QString message = portError(
            QString("Порт открыт на 9600, но переключение на %1 бод не удалось").arg(baudRate));
        m_serial->close();
        emit connectionStatus(false, message);
        return;
    }
#endif

    m_dataFlowEnabled = true;
    emit connectionStatus(true, QString("Подключено: %1, %2 бод")
                                    .arg(portName).arg(m_serial->baudRate()));
}

void SerialWorker::disconnectFromPort()
{
    m_buffer.clear();
    m_dataFlowEnabled = false;
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
        m_logSampleCount = 0;
        *m_logStream << "Time, s" << m_separator << "Ch1,  мм x 10^4" << m_separator << "Ch2,  мм x 10^4"
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

        // Один пакет: шесть каналов и давление, разделённые табуляцией.
        if (parts.size() != 7) continue;

        bool valid = true;
        for (int i = 0; i < parts.size(); ++i) {
            bool ok = false;
            const double value = parts[i].toDouble(&ok);
            if (!ok || !std::isfinite(value)) {
                valid = false;
                break;
            }
            if (i < 6) {
                const double mmValue = (value / SCALE_COEFF) * 1e4;
                parts[i] = QByteArray::number(mmValue, 'f', 6);
            }
        }
        if (!valid) continue;

        if (m_isLogging && m_logStream) {
            // Каждая записанная строка соответствует следующей десятой секунды.
            ++m_logSampleCount;
            *m_logStream << QString::number(m_logSampleCount / 10) << "."
                         << QString::number(m_logSampleCount % 10);

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
            QVector<QString> values;
            values.reserve(7);
            for (const auto &part : parts) {
                values.append(QString::fromUtf8(part));
            }
            emit dataReceived(values);
        }
    }
}
