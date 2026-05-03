#pragma once

#include <QMainWindow>

class QCloseEvent;

namespace blockforge
{
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
};
}
