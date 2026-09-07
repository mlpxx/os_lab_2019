#!/bin/bash

count=$#
sum=0

if [ "$count" -eq 0 ]; then
    echo "Нет аргументов"
    exit 1
fi

for number in "$@"
do
    sum=$((sum + number))
done


echo "Количество: $count"
echo "Среднее: $((sum / count))"