#pragma once

#include "ui_helpers.hpp"

#include <algorithm>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QCoreApplication>

namespace gmtools::gui {


class OverrideFields {
public:
    explicit OverrideFields(QFormLayout* form, const Overrides& initial = {},
                            const QString& keep = QStringLiteral("From source")) {
        sampling_ = new QLineEdit;
        sampling_->setPlaceholderText(keep);
        sampling_kind_ = new QComboBox;
        sampling_kind_->addItems({QCoreApplication::translate("gmtools::gui::OverrideFields", "dt (s)"), QCoreApplication::translate("gmtools::gui::OverrideFields", "fs (Hz)")});
        auto* timing = new QHBoxLayout;
        timing->addWidget(sampling_kind_);
        timing->addWidget(sampling_);
        form->addRow(QCoreApplication::translate("gmtools::gui::OverrideFields", "Sampling"), timing);

        quantity_ = new QComboBox;
        quantity_->addItem(keep);
        for (const auto value : {Quantity::Unknown, Quantity::Acceleration, Quantity::Velocity,
                                 Quantity::Displacement, Quantity::Counts})
            quantity_->addItem(as_text(name(value)), static_cast<int>(value));
        form->addRow(QCoreApplication::translate("gmtools::gui::OverrideFields", "Quantity"), quantity_);

        unit_ = new QComboBox;
        const auto update_units = [this, keep] {
            const auto previous = unit_->currentData();
            const auto selected = quantity_->currentData();
            unit_->clear();
            unit_->addItem(keep);
            for (auto unit : units()) {
                if (!selected.isValid() || selected.toInt() == static_cast<int>(Quantity::Unknown) ||
                    unit == Unit::Unknown || selected.toInt() == static_cast<int>(quantity_for(unit)))
                    unit_->addItem(as_text(name(unit)), static_cast<int>(unit));
            }
            unit_->setCurrentIndex(std::max(0, unit_->findData(previous)));
        };
        QObject::connect(quantity_, &QComboBox::currentIndexChanged, unit_, update_units);
        update_units();
        form->addRow(QCoreApplication::translate("gmtools::gui::OverrideFields", "Input unit"), unit_);
        unit_->setToolTip(QCoreApplication::translate("gmtools::gui::OverrideFields", "Assign the unit of the existing samples. This does not convert their values."));
        sampling_->setAccessibleName(QCoreApplication::translate("gmtools::gui::OverrideFields", "Sampling interval or rate"));
        sampling_kind_->setAccessibleName(QCoreApplication::translate("gmtools::gui::OverrideFields", "Sampling input type"));
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        set_options(initial);
    }

    void set_enabled(bool enabled) {
        sampling_->setEnabled(enabled);
        sampling_kind_->setEnabled(enabled);
        quantity_->setEnabled(enabled);
        unit_->setEnabled(enabled);
    }

    void set_options(const Overrides& options) {
        sampling_kind_->setCurrentIndex(options.fs_hz ? 1 : 0);
        sampling_->setText(options.fs_hz ? real(*options.fs_hz)
                          : options.dt_seconds ? real(*options.dt_seconds) : QString{});
        quantity_->setCurrentIndex(options.quantity ? quantity_->findData(static_cast<int>(*options.quantity)) : 0);
        unit_->setCurrentIndex(options.unit ? unit_->findData(static_cast<int>(*options.unit)) : 0);
    }

    Overrides options() const {
        Overrides result;
        const auto sampling = optional_number(sampling_, "Sampling requires a finite number");
        if (sampling_kind_->currentIndex() == 0)
            result.dt_seconds = sampling;
        else
            result.fs_hz = sampling;
        if (quantity_->currentData().isValid())
            result.quantity = static_cast<Quantity>(quantity_->currentData().toInt());
        if (unit_->currentData().isValid())
            result.unit = static_cast<Unit>(unit_->currentData().toInt());
        return result;
    }

private:
    QLineEdit* sampling_{};
    QComboBox* sampling_kind_{};
    QComboBox* quantity_{};
    QComboBox* unit_{};
};

} // namespace gmtools::gui
