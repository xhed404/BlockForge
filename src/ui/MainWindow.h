#pragma once

#include <QMainWindow>

namespace blockforge
{
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
};
}

