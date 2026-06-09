#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QVector>
#include <QChartView>
#include <QLineSeries>
#include <QTableWidget>
#include "ui_mainwindow.h"

class SerialWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnectClicked();
    void onTestStartClicked();
    void onTestStopClicked();
    void updateData(const QVector<QString> &values);
    void onConnectionStatus(bool connected, const QString &msg);
    void onError(const QString &msg);
    void setupChannelCheckboxes();

    void openTableWindow();
    void onTestFinishClicked();
    void setupLogCheckboxes();
    void updateLogMask();

private:
    void setupChart();
    void scanPorts();
    void setupPressureChart();
    Ui::MainWindow *ui;

    SerialWorker *m_worker;
    QThread *m_workerThread;

    QVector<QLineSeries*> m_series;
    QChart *m_chart;

    int m_pointCount;
    bool m_isTesting;

    bool m_isDarkTheme = false;
    QWidget *m_tableWindow = nullptr;
    QTableWidget *m_dataTable = nullptr;
    QWidget *m_pressureWindow = nullptr;
    QChart *m_pressureChart = nullptr;
    QLineSeries *m_pressureSeries = nullptr;

protected:
    void closeEvent(QCloseEvent *event) override;
};
#endif
