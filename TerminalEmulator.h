#ifndef TERMINALEMULATOR_H
#define TERMINALEMULATOR_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QSocketNotifier>
#include <QTextCharFormat>
#include <QComboBox>

class TerminalEmulator : public QWidget {
    Q_OBJECT

public:
    TerminalEmulator(QWidget *parent = nullptr);
    ~TerminalEmulator();

private slots:
    void readFromMaster();
    void sendInput();
    void changeBackgroundColor(const QString &colorName);
    void changeTextColor(const QString &colorName);

private:
    void appendFormattedText(const QString &text);
    void applyAnsiCodes(QTextCharFormat &format, const QStringList &codes);

    QPlainTextEdit *outputArea;
    QLineEdit *inputArea;
    QComboBox *backgroundColorComboBox;
    QComboBox *textColorComboBox;
    int master_fd;
    int slave_fd;
    QSocketNotifier *readNotifier;
    QColor currentTextColor;
};

#endif // TERMINALEMULATOR_H
