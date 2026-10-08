#include "CalibrationWizardDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace bdfrpc::studio {

CalibrationWizardDialog::CalibrationWizardDialog(QWidget* parent)
    : QDialog(parent) {
    build_ui();
    update_mode_ui();
}

CalibrationWizardDialog::~CalibrationWizardDialog() {
    stop_calibration();
}

void CalibrationWizardDialog::build_ui() {
    setWindowTitle("BDFR Calibration Wizard");
    resize(760, 620);

    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(
        "Use the same checkerboard dimensions and physical square size "
        "for every camera in a rig. Intrinsics should be solved first; "
        "then solve the synchronized rig extrinsics.",
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* form = new QFormLayout();

    mode_ = new QComboBox(this);
    mode_->addItem("Camera Intrinsics");
    mode_->addItem("Multi-Camera Rig Extrinsics");
    form->addRow("Mode:", mode_);

    device_index_ = new QSpinBox(this);
    device_index_->setRange(0, 63);
    form->addRow("Camera device index:", device_index_);

    reference_index_ = new QSpinBox(this);
    reference_index_->setRange(0, 63);
    form->addRow("Reference camera index:", reference_index_);

    columns_ = new QSpinBox(this);
    columns_->setRange(2, 30);
    columns_->setValue(9);
    form->addRow("Checkerboard inner columns:", columns_);

    rows_ = new QSpinBox(this);
    rows_->setRange(2, 30);
    rows_->setValue(6);
    form->addRow("Checkerboard inner rows:", rows_);

    square_size_ = new QDoubleSpinBox(this);
    square_size_->setRange(0.0001, 10.0);
    square_size_->setDecimals(5);
    square_size_->setSingleStep(0.005);
    square_size_->setValue(0.025);
    square_size_->setSuffix(" m");
    form->addRow("Square size:", square_size_);

    samples_ = new QSpinBox(this);
    samples_->setRange(5, 200);
    samples_->setValue(20);
    form->addRow("Accepted samples:", samples_);

    auto* profile_row = new QHBoxLayout();
    profile_path_ = new QLineEdit(this);
    profile_path_->setPlaceholderText(
        "Existing multi-camera intrinsics profile");
    browse_profile_button_ = new QPushButton("Browse…", this);
    profile_row->addWidget(profile_path_, 1);
    profile_row->addWidget(browse_profile_button_);
    form->addRow("Input profile:", profile_row);

    auto* output_row = new QHBoxLayout();
    output_path_ = new QLineEdit(this);
    output_path_->setText("cameras.bdfrcal");
    browse_output_button_ = new QPushButton("Browse…", this);
    output_row->addWidget(output_path_, 1);
    output_row->addWidget(browse_output_button_);
    form->addRow("Output profile:", output_row);

    root->addLayout(form);

    auto* run_row = new QHBoxLayout();
    start_button_ = new QPushButton("Start Calibration", this);
    stop_button_ = new QPushButton("Stop", this);
    stop_button_->setEnabled(false);
    run_row->addWidget(start_button_);
    run_row->addWidget(stop_button_);
    run_row->addStretch(1);
    root->addLayout(run_row);

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setPlaceholderText("Calibration output will appear here…");
    root->addWidget(log_, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Close,
        this);
    root->addWidget(buttons);

    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);

    connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
        update_mode_ui();
    });
    connect(browse_profile_button_, &QPushButton::clicked, this, [this] {
        browse_profile();
    });
    connect(browse_output_button_, &QPushButton::clicked, this, [this] {
        browse_output();
    });
    connect(start_button_, &QPushButton::clicked, this, [this] {
        start_calibration();
    });
    connect(stop_button_, &QPushButton::clicked, this, [this] {
        stop_calibration();
    });
    connect(
        process_,
        &QProcess::readyRead,
        this,
        [this] { append_process_output(); });
    connect(
        process_,
        &QProcess::finished,
        this,
        [this](int exit_code, QProcess::ExitStatus status) {
            append_process_output();
            start_button_->setEnabled(true);
            stop_button_->setEnabled(false);

            if (status == QProcess::NormalExit && exit_code == 0) {
                log_->appendPlainText(
                    "\nCalibration finished successfully.");
            } else {
                log_->appendPlainText(
                    QString(
                        "\nCalibration stopped/failed (exit %1).")
                        .arg(exit_code));
            }
        });
    connect(
        buttons,
        &QDialogButtonBox::rejected,
        this,
        &QDialog::reject);
}

void CalibrationWizardDialog::update_mode_ui() {
    const bool rig = mode_->currentIndex() == 1;

    device_index_->setEnabled(!rig);
    reference_index_->setEnabled(rig);
    profile_path_->setEnabled(rig);
    browse_profile_button_->setEnabled(rig);

    if (rig) {
        output_path_->setText("rig.bdfrcal");
    } else if (output_path_->text().isEmpty() ||
               output_path_->text() == "rig.bdfrcal") {
        output_path_->setText("cameras.bdfrcal");
    }
}

void CalibrationWizardDialog::browse_profile() {
    const auto path = QFileDialog::getOpenFileName(
        this,
        "Open BDFR Camera Profile",
        profile_path_->text(),
        "BDFR Calibration (*.bdfrcal *.txt);;All Files (*)");
    if (!path.isEmpty()) profile_path_->setText(path);
}

void CalibrationWizardDialog::browse_output() {
    const auto path = QFileDialog::getSaveFileName(
        this,
        "Save BDFR Calibration Profile",
        output_path_->text(),
        "BDFR Calibration (*.bdfrcal);;Text (*.txt)");
    if (!path.isEmpty()) output_path_->setText(path);
}

QString CalibrationWizardDialog::tool_path(
    const QString& base_name) const {

    const QString root =
        QString::fromUtf8(BDFRPC_BINARY_DIR);

#ifdef _WIN32
    const QString release =
        root + "/Release/" + base_name + ".exe";
    if (QFileInfo::exists(release)) return release;

    const QString debug =
        root + "/Debug/" + base_name + ".exe";
    if (QFileInfo::exists(debug)) return debug;

    return root + "/" + base_name + ".exe";
#else
    return root + "/" + base_name;
#endif
}

void CalibrationWizardDialog::start_calibration() {
    if (process_->state() != QProcess::NotRunning) return;

    const bool rig = mode_->currentIndex() == 1;
    const QString output = output_path_->text().trimmed();

    if (output.isEmpty()) {
        log_->appendPlainText("Select an output profile first.");
        return;
    }

    QString program;
    QStringList args;

    if (!rig) {
        program = tool_path("bdfrpc_calibrate_camera");
        args = {
            QString::number(device_index_->value()),
            QString::number(columns_->value()),
            QString::number(rows_->value()),
            QString::number(square_size_->value(), 'f', 6),
            QString::number(samples_->value()),
            output
        };
    } else {
        const QString profile = profile_path_->text().trimmed();
        if (profile.isEmpty()) {
            log_->appendPlainText(
                "Select the intrinsics profile before rig calibration.");
            return;
        }

        program = tool_path("bdfrpc_calibrate_rig");
        args = {
            profile,
            QString::number(reference_index_->value()),
            QString::number(columns_->value()),
            QString::number(rows_->value()),
            QString::number(square_size_->value(), 'f', 6),
            QString::number(samples_->value()),
            output
        };
    }

    if (!QFileInfo::exists(program)) {
        log_->appendPlainText(
            QString("Calibration tool not found: %1").arg(program));
        return;
    }

    log_->clear();
    log_->appendPlainText(
        QString("Starting: %1 %2\n")
            .arg(program, args.join(' ')));

    process_->setProgram(program);
    process_->setArguments(args);
    process_->start();

    if (!process_->waitForStarted(3000)) {
        log_->appendPlainText(
            "Failed to start calibration tool.");
        return;
    }

    start_button_->setEnabled(false);
    stop_button_->setEnabled(true);
}

void CalibrationWizardDialog::stop_calibration() {
    if (!process_ ||
        process_->state() == QProcess::NotRunning) {
        return;
    }

    process_->terminate();
    if (!process_->waitForFinished(1000)) {
        process_->kill();
        process_->waitForFinished(1000);
    }
}

void CalibrationWizardDialog::append_process_output() {
    const auto output = process_->readAll();
    if (!output.isEmpty()) {
        log_->appendPlainText(
            QString::fromUtf8(output).trimmed());
    }
}

} // namespace bdfrpc::studio
