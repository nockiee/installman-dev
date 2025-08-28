#!/bin/bash

# Скрипт установки Installman

echo "Установка Installman..."

# Проверка прав root
if [ "$EUID" -ne 0 ]; then
    echo "Запустите скрипт с правами root: sudo ./setup_installman.sh"
    exit 1
fi

# Проверка зависимостей
echo "Проверка зависимостей..."
if ! command -v g++ &> /dev/null; then
    echo "Установка компилятора C++..."
    apt-get install -y g++
fi

if ! pkg-config --exists gtk+-3.0; then
    echo "Установка GTK3..."
    apt-get install -y libgtk-3-dev
fi

if ! pkg-config --exists libarchive; then
    echo "Установка libarchive..."
    apt-get install -y libarchive-dev
fi

# Компиляция программы
echo "Компиляция программы..."
make

if [ $? -ne 0 ]; then
    echo "Ошибка компиляции!"
    exit 1
fi

# Установка
echo "Установка программы..."
make install

if [ $? -ne 0 ]; then
    echo "Ошибка установки!"
    exit 1
fi

echo "Установка завершена!"
echo "Теперь вы можете右键点击.tar.gz文件 -> Открыть с помощью -> Installman"