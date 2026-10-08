#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QSpinBox;

namespace bdfrpc::studio {

class CalibrationWizardDialog final : public QDialog {
public:
    explicit CalibrationWizardDialog(QWidget* parent = nullptr);
    ~CalibrationWizardDialog() override;

private:
    void build_ui();
    void update_mode_ui();
    void browse_profile();
    void browse_output();
    void start_calibration();
    void stop_calibration();
    QString tool_path(const QString& base_name) const;
    void append_process_output();

    QComboBox* mode_{nullptr};
    QSpinBox* device_index_{nullptr};
    QSpinBox* reference_index_{nullptr};
    QSpinBox* columns_{nullptr};
    QSpinBox* rows_{nullptr};
    QDoubleSpinBox* square_size_{nullptr};
    QSpinBox* samples_{nullptr};

    QLineEdit* profile_path_{nullptr};
    QLineEdit* output_path_{nullptr};
    QPushButton* browse_profile_button_{nullptr};
    QPushButton* browse_output_button_{nullptr};
    QPushButton* start_button_{nullptr};
    QPushButton* stop_button_{nullptr};

    QPlainTextEdit* log_{nullptr};
    QProcess* process_{nullptr};
};

} // namespace bdfrpc::studio
