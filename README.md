# SimpleFS

Файловая система создаётся поверх блочного устройства. После монтирования в `/mnt/simplefs` отображаются файлы вида `file0`, `file1`, `file2` и т.д.

## Сборка

```bash
make clean
make
```

После сборки должны появиться:

```text
simplefs.ko
simplefs_cli
```

## Запуск

Создаём тестовый образ и подключаем его как loop-устройство:

```bash
truncate -s 16M simplefs.img
LOOP=$(sudo losetup --find --show simplefs.img)
echo $LOOP
```

Пример вывода:

```text
/dev/loop9
```

Дальше в командах используется переменная `$LOOP`.

Загружаем модуль:

```bash
sudo insmod simplefs.ko imya_diska=$LOOP sb1_sektor=0 sb2_sektor=101 max_dlina_imeni=32 max_sektorov_file=4
```

Создаём точку монтирования и монтируем ФС:

```bash
sudo mkdir -p /mnt/simplefs
sudo mount -t simplefs $LOOP /mnt/simplefs
```

## Проверка VFS

```bash
cat /proc/filesystems | grep simplefs
mount | grep simplefs
ls -v /mnt/simplefs | head
find /mnt/simplefs -maxdepth 1 -type f | wc -l
```

Для образа `16M` и параметра `max_sektorov_file=4` ожидается примерно:

```text
8191
```

## Проверка superblock

Основной superblock:

```bash
sudo dd if=$LOOP bs=512 skip=0 count=1 2>/dev/null | hexdump -C | head
```

Копия superblock:

```bash
sudo dd if=$LOOP bs=512 skip=101 count=1 2>/dev/null | hexdump -C | head
```

## Проверка чтения и записи

```bash
echo 12345 | sudo tee /mnt/simplefs/file0
cat /mnt/simplefs/file0
```

Проверка другого файла:

```bash
echo hello | sudo tee /mnt/simplefs/file1
cat /mnt/simplefs/file1
```

## Проверка userspace-программой

Программа обходит все файлы, записывает в каждый случайное число, читает его обратно и сравнивает:

```bash
sudo ./simplefs_cli test /mnt/simplefs
```

В конце должен быть вывод вида:

```text
Checked files: 8191
```

## Проверка IOCTL

IOCTL вызывается через любой файл внутри смонтированной ФС, например через `/mnt/simplefs/file0`.

Получить mapping файла:

```bash
sudo ./simplefs_cli mapping /mnt/simplefs/file0 file3
```

Пример вывода:

```text
file3: start=13 size=4 sectors
```

Получить хэши файлов:

```bash
sudo ./simplefs_cli hashes /mnt/simplefs/file0 | head
```

Обнулить все файлы:

```bash
sudo ./simplefs_cli zero /mnt/simplefs/file0
```

Проверить результат можно так:

```bash
cat /mnt/simplefs/file0 | hexdump -C | head
```

Стереть FS:

```bash
sudo ./simplefs_cli erase /mnt/simplefs/file0
```

## Очистка

```bash
sudo umount /mnt/simplefs
sudo rmmod simplefs
sudo losetup -d $LOOP
rm simplefs.img
```