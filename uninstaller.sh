#!/bin/bash

if [ -z "$1" ]; then
    echo "Использование: $0 <имя_программы>"
    exit 1
fi

PROGRAM_NAME="$1"
INSTALL_DIR="/usr/local"

echo "Поиск установленных файлов для $PROGRAM_NAME..."

find "$INSTALL_DIR" -type f -name "*$PROGRAM_NAME*" -o -path "*/$PROGRAM_NAME/*" | while read -r file; do
    echo "Удаление: $file"
    sudo rm -f "$file"
done

find "$INSTALL_DIR" -type d -name "*$PROGRAM_NAME*" -empty | while read -r dir; do
    echo "Удаление директории: $dir"
    sudo rmdir "$dir" 2>/dev/null
done

echo "Поиск в bin..."
find "/usr/local/bin" -name "*$PROGRAM_NAME*" | while read -r bin_file; do
    echo "Удаление: $bin_file"
    sudo rm -f "$bin_file"
done

echo "Очистка завершена!"