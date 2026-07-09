#ifndef SERIALWORKER_H
#define SERIALWORKER_H

#include <QObject>
#include <QSerialPort>
#include <QFile>
#include <QTextStream>
#include <QVector>

class SerialWorker : public QObject
{
    Q_OBJECT
public:
    explicit SerialWorker(QObject *parent = nullptr);
    ~SerialWorker();

public slots:
    void connectToPort(const QString &portName, int baudRate);
    void disconnectFromPort();
    bool startLogging(const QString &filePath, const QString &separator = "\t");
    void stopLogging();
    void setDataFlowEnabled(bool enabled);
    void setLogChannelsMask(const QVector<int> &mask);

signals:
    void connectionStatus(bool connected, const QString &message);
    void dataReceived(const QVector<QString> &values);
    void errorOccurred(const QString &error);

private slots:
    void readData();

private:
    QString m_separator;
    int m_logPointCount;
    QSerialPort *m_serial;
    QFile *m_logFile;
    QTextStream *m_logStream;
    QByteArray m_buffer;
    bool m_isLogging;
    bool m_dataFlowEnabled;
    QVector<int> m_logChannelsMask;
};

#endif // SERIALWORKER_H
