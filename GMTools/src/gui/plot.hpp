#pragma once

#include "gmtools/gmtools.hpp"

#include <QWidget>

class TracePlot final : public QWidget {
public:
    TracePlot(const gmtools::Trace& trace, std::size_t channel, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    const gmtools::Trace& trace_;
    std::size_t channel_;
    double minimum_;
    double maximum_;
};
