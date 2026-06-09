#include "mainwindow.h"
#include "serialworker.h"
#include <QSerialPortInfo>
#include <QDateTime>
#include <QMessageBox>
#include <QDir>
#include <QtCharts/QValueAxis>
#include <QFileDialog>
#include <QColorDialog>
#include <QVBoxLayout>
#include <QStyleFactory>
#include <QApplication>
#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_pointCount(0), m_isTesting(false)
{
    ui->setupUi(this);
    setMinimumSize(1460, 800);
    setMaximumSize(1460, 800);

    m_worker = new SerialWorker();
    m_workerThread = new QThread();
    m_worker->moveToThread(m_workerThread);
    m_workerThread->start();

    connect(ui->pushButtonConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->pushButtonStart, &QPushButton::clicked, this, &MainWindow::onTestStartClicked);
    connect(ui->pushButtonStop, &QPushButton::clicked, this, &MainWindow::onTestStopClicked);

    connect(ui->pushButtonRefresh, &QPushButton::clicked, this, &MainWindow::scanPorts);
    connect(ui->pushButtonTable, &QPushButton::clicked, this, &MainWindow::openTableWindow);
    connect(ui->pushButtonFinish, &QPushButton::clicked, this, &MainWindow::onTestFinishClicked);

    connect(m_worker, &SerialWorker::dataReceived, this, &MainWindow::updateData);
    connect(m_worker, &SerialWorker::connectionStatus, this, &MainWindow::onConnectionStatus);
    connect(m_worker, &SerialWorker::errorOccurred, this, &MainWindow::onError);

    setupChannelCheckboxes();
    setupLogCheckboxes();
    setupChart();
    setupPressureChart();
    scanPorts();
}

MainWindow::~MainWindow()
{
    m_workerThread->quit();
    m_workerThread->wait();
    delete m_workerThread;
    delete m_worker;
    delete ui;
}


void MainWindow::setupChannelCheckboxes()
{
    QVector<QCheckBox*> checks = {
        ui->checkBoxVis1, ui->checkBoxVis2, ui->checkBoxVis3,
        ui->checkBoxVis4, ui->checkBoxVis5, ui->checkBoxVis6
    };

    for (int i = 0; i < 6; ++i) {
        connect(checks[i], &QCheckBox::toggled, this, [this, i](bool checked){
            if (m_series[i]) m_series[i]->setVisible(checked);
        });
    }

    // 7-й чекбокс привязываем к показу отдельного окна давления
    connect(ui->checkBoxVis7, &QCheckBox::toggled, this, [this](bool checked){
        if (m_pressureWindow) {
            m_pressureWindow->setVisible(checked);
            if (checked) m_pressureWindow->raise(); // Выводим на передний план
        }
    });
}

void MainWindow::setupLogCheckboxes()
{
    QVector<QCheckBox*> logChecks = {
        ui->checkBoxLog1, ui->checkBoxLog2, ui->checkBoxLog3,
        ui->checkBoxLog4, ui->checkBoxLog5, ui->checkBoxLog6,
        ui->checkBoxLog7
    };

    for (int i = 0; i < 7; ++i) {
        connect(logChecks[i], &QCheckBox::toggled, this, &MainWindow::updateLogMask);
    }
    updateLogMask();
}

void MainWindow::updateLogMask()
{
    QVector<QCheckBox*> logChecks = {
        ui->checkBoxLog1, ui->checkBoxLog2, ui->checkBoxLog3,
        ui->checkBoxLog4, ui->checkBoxLog5, ui->checkBoxLog6,
        ui->checkBoxLog7
    };

    QVector<int> mask;
    for (auto* cb : logChecks) {
        mask.append(cb->isChecked() ? 1 : 0);
    }

    QMetaObject::invokeMethod(m_worker, "setLogChannelsMask",
                              Qt::QueuedConnection,
                              Q_ARG(QVector<int>, mask));
}

void MainWindow::onTestFinishClicked()
{
    auto result = QMessageBox::question(this, "Завершение", "Завершить испытание и сохранить файл?");
    if (result == QMessageBox::No) return;

    m_isTesting = false;

    QMetaObject::invokeMethod(m_worker, "setDataFlowEnabled",
                              Qt::QueuedConnection, Q_ARG(bool, false));
    QMetaObject::invokeMethod(m_worker, "stopLogging", Qt::QueuedConnection);

    ui->pushButtonStart->setEnabled(true);
    ui->pushButtonStop->setEnabled(false);
    ui->pushButtonStop->setText("Стоп");
    ui->pushButtonFinish->setEnabled(false);

    ui->comboBoxPort->setEnabled(true);
    ui->comboBoxBaud->setEnabled(true);
    ui->pushButtonRefresh->setEnabled(true);
    ui->pushButtonConnect->setEnabled(true);

    ui->labelStatus->setText("Статус: Испытание завершено");
    ui->statusbar->showMessage("Данные успешно сохранены в файл.");
}

void MainWindow::setupChart()
{
    m_chart = new QChart();
    m_chart->setTitle("График испытаний");
    m_chart->setAnimationOptions(QChart::NoAnimation);

    QValueAxis *axisX = new QValueAxis();
    axisX->setTitleText("Время, с");
    m_chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis *axisY = new QValueAxis();
    axisY->setTitleText("Значение, мм x 10^4");
    axisY->setMin(0);
    axisY->setMax(1030);
    m_chart->addAxis(axisY, Qt::AlignLeft);

    QVector<QColor> colors = {Qt::red, Qt::blue, Qt::green, Qt::darkYellow, Qt::magenta, Qt::cyan, Qt::black};
    for (int i = 0; i < 6; ++i) {
        QLineSeries *series = new QLineSeries();
        series->setName(QString("Канал%1").arg(i + 1));
        series->setColor(colors[i]);
        m_chart->addSeries(series);
        series->attachAxis(axisX);
        series->attachAxis(axisY);
        m_series.append(series);
    }

    ui->chartView->setChart(m_chart);
    ui->chartView->setContentsMargins(0, 0, 0, 0);
    ui->chartView->setRenderHint(QPainter::Antialiasing);

    ui->chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void MainWindow::setupPressureChart()
{
    m_pressureWindow = new QWidget(this, Qt::Window);
    m_pressureWindow->setWindowTitle("График давления");
    m_pressureWindow->resize(700, 400);

    QVBoxLayout *layout = new QVBoxLayout(m_pressureWindow);
    QChartView *view = new QChartView(m_pressureWindow);

    m_pressureChart = new QChart();
    m_pressureChart->setTitle("Давление, bar");
    m_pressureChart->setAnimationOptions(QChart::NoAnimation);

    QValueAxis *axisX = new QValueAxis();
    axisX->setTitleText("Время, с");
    m_pressureChart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis *axisY = new QValueAxis();
    axisY->setTitleText("Давление, bar");
    m_pressureChart->addAxis(axisY, Qt::AlignLeft);

    m_pressureSeries = new QLineSeries();
    m_pressureSeries->setName("Давление (Ch7)");
    m_pressureSeries->setColor(Qt::darkRed);

    m_pressureChart->addSeries(m_pressureSeries);
    m_pressureSeries->attachAxis(axisX);
    m_pressureSeries->attachAxis(axisY);

    view->setChart(m_pressureChart);
    view->setRenderHint(QPainter::Antialiasing);
    layout->addWidget(view);
}

void MainWindow::scanPorts()
{
    ui->comboBoxPort->clear();
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        ui->comboBoxPort->addItem(info.portName());
    }
}

void MainWindow::onConnectClicked()
{
    if (ui->pushButtonConnect->text().contains("Отключ", Qt::CaseInsensitive) ||
        ui->labelStatus->text().contains("Подключено"))
    {
        QMetaObject::invokeMethod(m_worker, "disconnectFromPort", Qt::QueuedConnection);
        return;
    }

    QString portName = ui->comboBoxPort->currentText();
    if (portName.isEmpty() || portName.contains("—") || portName.contains("Нет"))
    {
        QMessageBox::warning(this, "Порт", "Выберите корректный COM-порт");
        return;
    }

    int baud = ui->comboBoxBaud->currentText().toInt();
    if (baud <= 0) baud = 115200;

    QMetaObject::invokeMethod(m_worker, "connectToPort",
                              Qt::QueuedConnection,
                              Q_ARG(QString, portName),
                              Q_ARG(int, baud));

    ui->pushButtonConnect->setEnabled(false);
}

void MainWindow::onConnectionStatus(bool connected, const QString &msg)
{
    ui->pushButtonConnect->setEnabled(true);

    if (connected)
    {
        ui->pushButtonConnect->setText("Отключить");
        ui->pushButtonStart->setEnabled(true);
        ui->labelStatus->setText("Статус: Подключено");
    }
    else
    {
        ui->pushButtonConnect->setText("Подключить");
        ui->pushButtonStart->setEnabled(false);
        ui->labelStatus->setText("Статус: Отключено");
    }

    ui->statusbar->showMessage(msg);

    if (!connected && m_isTesting)
        onTestStopClicked();
}

void MainWindow::onTestStartClicked()
{

    QString defaultDir = QDir::currentPath() + "/Logs";
    QDir().mkpath(defaultDir);

    QString defaultFileName = "test_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
    QString proposedPath = defaultDir + "/" + defaultFileName;

    QString selectedFilter;
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Сохранить результаты испытания как...",
        proposedPath,
        "CSV файлы (*.csv);;Текстовые файлы (*.txt)",
        &selectedFilter
        );

    if (fileName.isEmpty()) {
        ui->statusbar->showMessage("Сохранение отменено");
        return;
    }

    QString separator = "\t";
    if (selectedFilter.contains("*.csv")) {
        if (!fileName.endsWith(".csv", Qt::CaseInsensitive)) {
            QFileInfo info(fileName);
            fileName = info.absolutePath() + "/" + info.baseName() + ".csv";
        }
        separator = ";";
    }
    else if (selectedFilter.contains("*.txt")) {
        if (!fileName.endsWith(".txt", Qt::CaseInsensitive)) {
            QFileInfo info(fileName);
            fileName = info.absolutePath() + "/" + info.baseName() + ".txt";
        }
        separator = "\t";
    }

    bool loggingStarted = false;
    QMetaObject::invokeMethod(m_worker, "startLogging",
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, loggingStarted),
                              Q_ARG(QString, fileName),
                              Q_ARG(QString, separator));

    if (!loggingStarted) {
        ui->statusbar->showMessage("Ошибка: Испытание не запущено, так как файл лога не создан.");
        return;
    }

    QMetaObject::invokeMethod(m_worker, "setDataFlowEnabled",
                              Qt::QueuedConnection,
                              Q_ARG(bool, true));

    for (QLineSeries *series : m_series) {
        series->clear();
    }

    if (m_pressureSeries) {
        m_pressureSeries->clear();
    }

    QValueAxis *axisX = qobject_cast<QValueAxis*>(m_chart->axes(Qt::Horizontal).first());
    if (axisX) {
        axisX->setRange(0, 50);
    }


    m_isTesting = true;
    m_pointCount = 0;

    if (m_dataTable) m_dataTable->setRowCount(0);

    ui->pushButtonStart->setEnabled(false);
    ui->pushButtonStop->setEnabled(true);
    ui->comboBoxPort->setEnabled(false);
    ui->comboBoxBaud->setEnabled(false);
    ui->pushButtonFinish->setEnabled(true);
    ui->pushButtonRefresh->setEnabled(false);
    ui->pushButtonConnect->setEnabled(false);
    ui->labelStatus->setText("Статус: ИСПЫТАНИЕ ИДЕТ");
    ui->statusbar->showMessage("Файл: " + QFileInfo(fileName).fileName());
}

void MainWindow::onTestStopClicked()
{
    if (ui->pushButtonStop->text() == "СТОП")
    {
        m_isTesting = false;

        QMetaObject::invokeMethod(m_worker, "setDataFlowEnabled",
                                  Qt::QueuedConnection,
                                  Q_ARG(bool, false));

        ui->pushButtonStop->setText("Продолжить");
        ui->labelStatus->setText("Статус: ПАУЗА");
        ui->statusbar->showMessage("Испытание приостановлено");
    }
    else
    {
        m_isTesting = true;

        QMetaObject::invokeMethod(m_worker, "setDataFlowEnabled",
                                  Qt::QueuedConnection,
                                  Q_ARG(bool, true));

        ui->pushButtonStop->setText("СТОП");
        ui->labelStatus->setText("Статус: ИСПЫТАНИЕ ИДЕТ");
        ui->statusbar->showMessage("Испытание возобновлено");
    }
}


void MainWindow::updateData(const QVector<QString> &values)
{
    int count = qMin(values.size(), 7);

    QVector<QLabel*> labels = {
        ui->labelVal1, ui->labelVal2, ui->labelVal3,
        ui->labelVal4, ui->labelVal5, ui->labelVal6,
        ui->labelVal7
    };

    for (int i = 0; i < qMin(count, labels.size()); ++i) {
        labels[i]->setText(values[i]);
    }

    // --- Обновление 6 главных тензодатчиков ---
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    bool hasMainData = false;
    int visibleRange = 50;

    for (int i = 0; i < qMin(count, 6); ++i) {
        if (m_series[i]->isVisible()) {
            if (m_series[i]->count() > 2000) m_series[i]->remove(0); // Буфер в 2000 точек

            double val = values[i].toDouble();
            m_series[i]->append(m_pointCount, val);

            // Динамический поиск мин/макс только среди видимых точек
            auto points = m_series[i]->points();
            int startIdx = qMax(0, points.size() - visibleRange);
            for (int j = startIdx; j < points.size(); ++j) {
                if (points[j].y() < minY) minY = points[j].y();
                if (points[j].y() > maxY) maxY = points[j].y();
                hasMainData = true;
            }
        }
    }

    // --- Обновление графика давления (7-й канал) ---
    double pMinY = std::numeric_limits<double>::max();
    double pMaxY = std::numeric_limits<double>::lowest();
    bool hasPressureData = false;

    if (count > 6 && m_pressureSeries) {
        if (m_pressureSeries->count() > 2000) m_pressureSeries->remove(0);

        double pVal = values[6].toDouble();
        m_pressureSeries->append(m_pointCount, pVal);

        auto points = m_pressureSeries->points();
        int startIdx = qMax(0, points.size() - visibleRange);
        for (int j = startIdx; j < points.size(); ++j) {
            if (points[j].y() < pMinY) pMinY = points[j].y();
            if (points[j].y() > pMaxY) pMaxY = points[j].y();
            hasPressureData = true;
        }
    }

    // --- Динамическое масштабирование X и Y для главного графика ---
    QValueAxis *axisX = qobject_cast<QValueAxis*>(m_chart->axes(Qt::Horizontal).first());
    QValueAxis *axisY = qobject_cast<QValueAxis*>(m_chart->axes(Qt::Vertical).first());

    if (axisX) {
        axisX->setRange(m_pointCount > visibleRange ? m_pointCount - visibleRange : 0,
                        qMax(m_pointCount, visibleRange));
    }
    if (axisY && hasMainData) {
        double padding = (maxY - minY) * 0.1; // Отступы сверху и снизу в 10%
        if (padding == 0) padding = 5.0; // Если линия прямая
        axisY->setRange(minY - padding, maxY + padding);
    }

    if (m_pressureChart) {
        QValueAxis *pAxisX = qobject_cast<QValueAxis*>(m_pressureChart->axes(Qt::Horizontal).first());
        QValueAxis *pAxisY = qobject_cast<QValueAxis*>(m_pressureChart->axes(Qt::Vertical).first());

        if (pAxisX) {
            pAxisX->setRange(m_pointCount > visibleRange ? m_pointCount - visibleRange : 0,
                             qMax(m_pointCount, visibleRange));
        }
        if (pAxisY && hasPressureData) {
            double pPadding = (pMaxY - pMinY) * 0.1;
            if (pPadding == 0) pPadding = 1.0;
            pAxisY->setRange(pMinY - pPadding, pMaxY + pPadding);
        }
    }

    m_pointCount++;

    // --- Обновление таблицы (без изменений) ---
    if (m_tableWindow && m_tableWindow->isVisible()) {
        int row = m_dataTable->rowCount();
        m_dataTable->insertRow(row);
        m_dataTable->setItem(row, 0, new QTableWidgetItem(QDateTime::currentDateTime().toString("hh:mm:ss.zzz")));
        for (int i = 0; i < count; ++i) {
            m_dataTable->setItem(row, i + 1, new QTableWidgetItem(values[i]));
        }
        m_dataTable->scrollToBottom();
    }
}

void MainWindow::openTableWindow()
{
    if (!m_tableWindow) {
        m_tableWindow = new QWidget();
        m_tableWindow->setWindowTitle("Таблица данных в реальном времени");
        m_tableWindow->resize(800, 600);

        QVBoxLayout *layout = new QVBoxLayout(m_tableWindow);
        m_dataTable = new QTableWidget(0, 8, m_tableWindow);
        m_dataTable->setHorizontalHeaderLabels({"Время", "Ch1", "Ch2", "Ch3", "Ch4", "Ch5", "Ch6", "Pressure"});

        layout->addWidget(m_dataTable);
    }
    m_tableWindow->show();
    m_tableWindow->raise();
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_isTesting) {
        auto result = QMessageBox::warning(this, "Предупреждение",
                                           "Испытание еще идет! Вы уверены, что хотите выйти? "
                                                                                    "Данные будут сохранены и закрыты.",
                                           QMessageBox::Yes | QMessageBox::No);
        if (result == QMessageBox::Yes) {
            onTestFinishClicked();
            event->accept();
        } else {
            event->ignore();
        }
    } else {
        event->accept();
    }
}

void MainWindow::onError(const QString &msg)
{
    QMessageBox::critical(this, "Ошибка", msg);
    if (m_isTesting) onTestStopClicked();
}
