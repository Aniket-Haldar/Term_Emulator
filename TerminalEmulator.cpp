// TerminalEmulator.cpp
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

TerminalEmulator::TerminalEmulator(QWidget *parent) : QWidget(parent), outputArea(nullptr), inputArea(nullptr), master_fd(-1), slave_fd(-1), readNotifier(nullptr) {
    // Setup the UI
    outputArea = new QPlainTextEdit(this);
    //inputArea = new QLineEdit(this);
    inputArea = new QLineEdit(this);
    inputArea->setFocus();
    //inputArea->setAcceptRichText(false);
    outputArea->setReadOnly(true);
    // Set the default background color to blue for retro look
    outputArea->setStyleSheet("QPlainTextEdit { background-color: blue; color: white;font-weight:900 }");
    inputArea->setStyleSheet("QLineEdit { background-color: black; color: white; }");


    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(outputArea);
    layout->addWidget(inputArea);

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
        // Connect key press event
        //connect(inputArea, &QTextEdit::textChanged, this, &TerminalEmulator::sendInput);

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


        // Remove ANSI escape sequences
        output.remove(QRegularExpression("\x1B\\[[0-9;]*[a-zA-Z]")); // CSI sequences
        output.remove(QRegularExpression("\x1B\\][^\\x07]*\\x07"));  // OSC sequences
        output.remove(QRegularExpression("\x1B\\[\\?2004[h|l]"));    // Bracketed paste mode sequences
        output.remove(QRegularExpression("\x1B\\(B"));               // Character set sequence
        output.remove(QRegularExpression("\x1B\\]0;[^\\x07]*\\x07"));// Title setting sequences
        output.remove(QRegularExpression("[\x00-\x1F\x7F]"));           // Remove other non-printable control characters

        outputArea->appendPlainText(output);
    } else if (count == 0) { // EOF
        readNotifier->setEnabled(false);
    } else { // Error
        perror("read");
    }
}
// void TerminalEmulator::keyPressEvent(QKeyEvent *event) {
//     if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
//         // Send input when Enter is pressed
//         sendInput();
//     } else {
//         // Process other keys normallly
//         QWidget::keyPressEvent(event);
//     }
// }



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

