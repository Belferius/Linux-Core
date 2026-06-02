# SimpleFS

Учебная файловая система для Linux kernel module.

## Сборка

```bash
make clean
make
```

## Запуск

```bash
truncate -s 16M simplefs.img
LOOP=$(sudo losetup --find --show simplefs.img)
echo $LOOP
```

```bash
sudo insmod simplefs.ko disk_name=$LOOP sb_first_sector=0 sb_second_sector=101 max_name_len=32 max_file_sectors=4
sudo mkdir -p /mnt/simplefs
sudo mount -t simplefs $LOOP /mnt/simplefs
```

## Проверка

```bash
cat /proc/filesystems | grep simplefs
ls -v /mnt/simplefs | head
find /mnt/simplefs -maxdepth 1 -type f | wc -l
```

Для образа `16M` и `max_file_sectors=4` ожидается `8191` файл.

## Проверка чтения и записи

```bash
echo 12345 | sudo tee /mnt/simplefs/file0
cat /mnt/simplefs/file0
```

## Проверка userspace-программой

```bash
sudo ./simplefs_cli test /mnt/simplefs
```

## IOCTL

```bash
sudo ./simplefs_cli mapping /mnt/simplefs/file0 file3
sudo ./simplefs_cli hashes /mnt/simplefs/file0 | head
sudo ./simplefs_cli zero /mnt/simplefs/file0
sudo ./simplefs_cli erase /mnt/simplefs/file0
```

## Очистка

```bash
sudo umount /mnt/simplefs 2>/dev/null
sudo rmmod simplefs
sudo losetup -d $LOOP
rm simplefs.img
```