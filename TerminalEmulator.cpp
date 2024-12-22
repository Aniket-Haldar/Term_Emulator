#include "TerminalEmulator.h"

#include <QVBoxLayout>
#include <QApplication>
#include <QTimer>
#include <QSocketNotifier>
#include <QRegularExpression>
#include <pty.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <iostream>
#include <cstdlib>

TerminalEmulator::TerminalEmulator(QWidget *parent) : QWidget(parent), outputArea(nullptr), inputArea(nullptr), backgroundColorComboBox(nullptr), textColorComboBox(nullptr), master_fd(-1), slave_fd(-1), readNotifier(nullptr), currentTextColor(Qt::white) {
    // Setup the UI
    outputArea = new QPlainTextEdit(this);
    inputArea = new QLineEdit(this);
    inputArea->setFocus();
    outputArea->setReadOnly(true);

    // Set default styles for output and input areas
    outputArea->setStyleSheet("QPlainTextEdit { background-color: black; color: white; font-weight: bold; }");
    inputArea->setStyleSheet("QLineEdit { background-color: black; color: white; }");

    // Setup the background colour selection combo box
    backgroundColorComboBox = new QComboBox(this);
    backgroundColorComboBox->addItem("Black", "black");
    backgroundColorComboBox->addItem("Red", "red");
    backgroundColorComboBox->addItem("Green", "green");
    backgroundColorComboBox->addItem("Yellow", "yellow");
    backgroundColorComboBox->addItem("Blue", "blue");
    backgroundColorComboBox->addItem("Magenta", "magenta");
    backgroundColorComboBox->addItem("Cyan", "cyan");
    backgroundColorComboBox->addItem("White", "white");

    connect(backgroundColorComboBox, &QComboBox::currentTextChanged, this, &TerminalEmulator::changeBackgroundColor);

    // Setup the text colour selection combo box
    textColorComboBox = new QComboBox(this);
    textColorComboBox->addItem("White", "white");
    textColorComboBox->addItem("Black", "black");
    textColorComboBox->addItem("Red", "red");
    textColorComboBox->addItem("Green", "green");
    textColorComboBox->addItem("Yellow", "yellow");
    textColorComboBox->addItem("Blue", "blue");
    textColorComboBox->addItem("Magenta", "magenta");
    textColorComboBox->addItem("Cyan", "cyan");

    connect(textColorComboBox, &QComboBox::currentTextChanged, this, &TerminalEmulator::changeTextColor);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(outputArea);
    layout->addWidget(inputArea);
    layout->addWidget(backgroundColorComboBox);
    layout->addWidget(textColorComboBox);

    setLayout(layout);

    // Setup pseudo-terminal
    if (openpty(&master_fd, &slave_fd, nullptr, nullptr, nullptr) == -1) {
        perror("openpty");
        exit(1);
    }

    // Fork the child process
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) { // Child process
        ::close(master_fd); // Close master in child process
        setsid();
        if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
            perror("ioctl");
            exit(1);
        }
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        ::close(slave_fd); // Close slave after duplication
        // Set the TERM environment variable
        if (setenv("TERM", "xterm-256color", 1) == -1) {
            perror("setenv");
            exit(1);
        }
        const char *logMessage = "Slave terminal started successfully.\n";
        write(STDOUT_FILENO, logMessage, strlen(logMessage));
        execlp("/bin/bash", "bash", nullptr);
        perror("execlp");
        exit(1);
    } else { // Parent process
        ::close(slave_fd); // Close slave in parent process

        // Monitor master_fd for output
        readNotifier = new QSocketNotifier(master_fd, QSocketNotifier::Read, this);
        connect(readNotifier, &QSocketNotifier::activated, this, &TerminalEmulator::readFromMaster);

        // Handle user input
        connect(inputArea, &QLineEdit::returnPressed, this, &TerminalEmulator::sendInput);
    }
}

TerminalEmulator::~TerminalEmulator() {
    ::close(master_fd); // Explicitly close the master file descriptor
}

void TerminalEmulator::readFromMaster() {
    char buffer[256];
    ssize_t count = read(master_fd, buffer, sizeof(buffer) - 1);
    if (count > 0) {
        buffer[count] = '\0'; // Null-terminate buffer
        QString output = QString::fromLocal8Bit(buffer);

        if (output.contains("\033[H\033[2J")) {
            outputArea->clear(); // Clear the terminal output
            output.remove(QRegularExpression("\033\\[H\\033\\[2J")); // Remove the sequence
        }

        // Remove unsupported sequences
        output.remove(QRegularExpression("\033\\[\\?2004[h|l]")); // Bracketed paste mode
        output.remove(QRegularExpression("\033\\[3J")); // Clear scrollback buffer
        output.remove(QRegularExpression("\x1B\\]0;.*\x07")); // Set terminal title

        appendFormattedText(output);

    } else if (count == 0) { // EOF
        readNotifier->setEnabled(false);
    } else { // Error
        perror("read");
    }
}

void TerminalEmulator::appendFormattedText(const QString &text) {
    QTextCursor cursor(outputArea->textCursor());
    cursor.movePosition(QTextCursor::End);

    QRegularExpression re("\033\\[([0-9;]*)m");
    QRegularExpressionMatchIterator i = re.globalMatch(text);
    int lastEnd = 0;

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString code = match.captured(1);
        QString preText = text.mid(lastEnd, match.capturedStart() - lastEnd);
        lastEnd = match.capturedEnd();

        QTextCharFormat format = cursor.charFormat();
        applyAnsiCodes(format, code.split(';'));
        cursor.insertText(preText, format);
    }

    cursor.insertText(text.mid(lastEnd));
}

void TerminalEmulator::applyAnsiCodes(QTextCharFormat &format, const QStringList &codes) {
    for (const QString &code : codes) {
        int val = code.toInt();
        switch (val) {
        case 0: // Reset
            format = QTextCharFormat();
            break;
        case 1: // Bold
            format.setFontWeight(QFont::Bold);
            break;
        // Background colours
        case 40: format.setBackground(Qt::black); break;
        case 41: format.setBackground(Qt::red); break;
        case 42: format.setBackground(Qt::green); break;
        case 43: format.setBackground(Qt::yellow); break;
        case 44: format.setBackground(Qt::blue); break;
        case 45: format.setBackground(Qt::magenta); break;
        case 46: format.setBackground(Qt::cyan); break;
        case 47: format.setBackground(Qt::white); break;

        }
    }
}

void TerminalEmulator::sendInput() {
    QString input = inputArea->text() + "\n";
    qDebug() << "Sending input to master_fd:" << input;
    ssize_t bytesWritten = write(master_fd, input.toLocal8Bit().constData(), input.size());
    if (bytesWritten == -1) {
        perror("write failed");
    } else {
        qDebug() << "Bytes written:" << bytesWritten;
    }
    inputArea->clear();
}

void TerminalEmulator::changeBackgroundColor(const QString &colorName) {
    QString colorStyleSheet = QString("QPlainTextEdit { background-color: %1; color: %2; font-weight: bold; } QLineEdit { background-color: %1; color: %2; }").arg(colorName).arg(currentTextColor.name());
    outputArea->setStyleSheet(colorStyleSheet);
    inputArea->setStyleSheet(colorStyleSheet);
}

void TerminalEmulator::changeTextColor(const QString &colorName) {
    currentTextColor = QColor(colorName);
    QString colorStyleSheet = QString("QPlainTextEdit { background-color: %1; color: %2; font-weight: bold; } QLineEdit { background-color: %1; color: %2; }").arg(backgroundColorComboBox->currentData().toString()).arg(colorName);
    outputArea->setStyleSheet(colorStyleSheet);
    inputArea->setStyleSheet(colorStyleSheet);
}
