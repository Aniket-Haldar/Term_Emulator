#include "TerminalEmulator.h"

#include <QVBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QSocketNotifier>
#include <QRegularExpression>
#include <pty.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <iostream>
#include <cstdlib>

TerminalEmulator::TerminalEmulator(QWidget *parent)
    : QWidget(parent), outputArea(nullptr), inputArea(nullptr), master_fd(-1), slave_fd(-1), readNotifier(nullptr) {
    // Setup the UI
    outputArea = new QTextEdit(this);
    inputArea = new QLineEdit(this);
    inputArea->setFocus();
    outputArea->setReadOnly(true);

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

        // Handle clear screen sequence
        if (output.contains("\033[H\033[2J")) {
            outputArea->clear();
            output.remove(QRegularExpression("\033\\[H\\033\\[2J"));
        }

        // Parse and apply ANSI escape sequences
        parseAnsiSequences(output);
    } else if (count == 0) { // EOF
        readNotifier->setEnabled(false);
    } else { // Error
        perror("read");
    }
}

void TerminalEmulator::parseAnsiSequences(const QString &text) {
    static const QRegularExpression ansiRegex("\033\\[([0-9;]*)m");
    QTextCursor cursor(outputArea->textCursor());
    cursor.movePosition(QTextCursor::End);

    int lastPos = 0;
    QRegularExpressionMatch match;

    while ((match = ansiRegex.match(text, lastPos)).hasMatch()) {
        // Append text before the escape sequence
        cursor.insertText(text.mid(lastPos, match.capturedStart() - lastPos));

        // Get the ANSI codes
        QStringList codes = match.captured(1).split(';');
        applyAnsiCodes(cursor, codes);

        lastPos = match.capturedEnd();
    }

    // Append the remaining text
    cursor.insertText(text.mid(lastPos));
}

void TerminalEmulator::applyAnsiCodes(QTextCursor &cursor, const QStringList &codes) {
    QTextCharFormat format = cursor.charFormat();

    for (const QString &code : codes) {
        bool ok;
        int n = code.toInt(&ok);
        if (!ok) continue;

        if (n == 0) {
            // Reset all attributes
            format = QTextCharFormat();
        } else if (n == 1) {
            // Bold
            format.setFontWeight(QFont::Bold);
        } else if (n == 22) {
            // Normal weight
            format.setFontWeight(QFont::Normal);
        } else if (n >= 30 && n <= 37) {
            // Set foreground color
            format.setForeground(ansiColor(n - 30));
        } else if (n == 39) {
            // Default foreground color
            format.setForeground(QBrush());
        } else if (n >= 40 && n <= 47) {
            // Set background color
            format.setBackground(ansiColor(n - 40));
        } else if (n == 49) {
            // Default background color
            format.setBackground(QBrush());
        }
        // Add more cases as needed for other ANSI codes
    }

    cursor.setCharFormat(format);
}

QColor TerminalEmulator::ansiColor(int index) {
    static const QColor ansiColors[8] = {
        QColor(0, 0, 0),       // Black
        QColor(128, 0, 0),     // Red
        QColor(0, 128, 0),     // Green
        QColor(128, 128, 0),   // Yellow
        QColor(0, 0, 128),     // Blue
        QColor(128, 0, 128),   // Magenta
        QColor(0, 128, 128),   // Cyan
        QColor(192, 192, 192)  // White
    };

    if (index >= 0 && index < 8) {
        return ansiColors[index];
    } else {
        return QColor(); // Default color
    }
}

void TerminalEmulator::sendInput() {
    QString input = inputArea->text() + "\n";
    ssize_t bytesWritten = write(master_fd, input.toLocal8Bit().constData(), input.size());
    if (bytesWritten == -1) {
        perror("write failed");
    }
    inputArea->clear();
}
